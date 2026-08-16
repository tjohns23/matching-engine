// generate_market_data.cpp
//
// Produces a deterministic, reproducible synthetic order-flow dataset for
// replay against OrderBook in simulation.cpp. Designed to be single-sourced:
// the same dataset feeds both the invariant-checking loop and the
// performance-profiling loop, so results from the two are comparable and
// bugs found in one are reproducible in the other.
//
// Usage:
//   ./generate_market_data <num_ticks> <output.csv> [seed]
//
// Output format (CSV, one line per operation):
//   op,id,side,type,price,qty,timestamp
//     op        NEW | CANCEL
//     id        order id (uint64)
//     side      BUY | SELL          (empty for CANCEL)
//     type      LIMIT | MARKET      (empty for CANCEL)
//     price     integer price       (empty for CANCEL, empty for MARKET NEW)
//     qty       integer quantity    (empty for CANCEL)
//     timestamp monotonically increasing tick/sequence index
//
// Design notes:
//   - Price offsets drawn from a discretized Laplace distribution around a
//     slowly random-walking mid price -> realistic clustering near the touch
//     with fat tails, rather than uniform price selection.
//   - Order sizes drawn from a log-normal distribution -> many small orders,
//     occasional large ones, matching real order-size skew.
//   - A two-state Markov chain (QUIET / BURST) drives arrival intensity and
//     side imbalance over time, producing bursty, directionally-biased flow
//     instead of a flat, balanced Poisson process.
//   - A fraction of resting orders are scheduled for cancellation some
//     number of ticks after insertion, so CANCEL and the empty-level
//     cleanup path both get realistic exercise downstream. Scheduled
//     cancels are kept in a min-heap ordered by fire_at_tick (not a plain
//     FIFO queue) since cancel_delay is randomized -- a far-future cancel
//     can be scheduled before a near-future one, so only a priority queue
//     guarantees the soonest-due cancel always fires first.
//
// Structure: simulation state and distribution objects are grouped into
// SimState / Distributions so each tick-phase can be its own function with
// a self-documenting signature, rather than one long main() closing over a
// dozen loose locals via lambda captures.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <vector>

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Qty = std::int64_t;

enum class Regime { Quiet, Burst };

struct PendingCancel {
  OrderId id;
  std::uint64_t fire_at_tick;
};

// Order by fire_at_tick so std::priority_queue (a max-heap by default)
// surfaces the SOONEST-due cancel at the top when used with this comparator
// (see the priority_queue member below, which this flips into a min-heap).
struct PendingCancelLater {
  bool operator()(const PendingCancel &a, const PendingCancel &b) const {
    return a.fire_at_tick > b.fire_at_tick;
  }
};

namespace {

// Draw from a discretized Laplace distribution centered at 0 with the given
// scale (b). Laplace has fatter tails than Gaussian, which better matches
// how order book depth actually falls off away from the touch. <random>
// has no built-in laplace_distribution, so this is the standard
// inverse-CDF construction on top of uniform_real_distribution.
Price sample_laplace_offset(std::mt19937_64 &rng, double scale) {
  std::uniform_real_distribution<double> u(-0.5, 0.5);
  double x = u(rng);
  double sign = (x < 0.0) ? -1.0 : 1.0;
  double magnitude = -scale * std::log(1.0 - 2.0 * std::abs(x));
  return static_cast<Price>(std::llround(sign * magnitude));
}

} // namespace

// ---------------------------------------------------------------------
// Simulation state: everything that evolves tick-by-tick.
// ---------------------------------------------------------------------
struct SimState {
  std::mt19937_64 rng;

  double mid_price = 10000.0; // integer-price ticks, e.g. cents
  Regime regime = Regime::Quiet;
  double side_bias = 0.0; // -1..+1, skews P(buy) away from 0.5 during bursts

  OrderId next_id = 1;
  std::uint64_t timestamp = 0;

  // Min-heap ordered by fire_at_tick -- see PendingCancelLater above.
  std::priority_queue<PendingCancel, std::vector<PendingCancel>,
                      PendingCancelLater>
      pending_cancels;

  explicit SimState(std::uint64_t seed) : rng(seed) {}
};

// ---------------------------------------------------------------------
// Distribution objects: fixed shape for the whole run, parameters chosen
// once up front. Grouped here so each tick-phase function can take a
// single `dist` argument instead of half a dozen individual ones.
// ---------------------------------------------------------------------
struct Distributions {
  // Mid-price random walk step.
  std::normal_distribution<double> mid_walk{0.0, 1.5};

  // Log-normal order size: mean/stddev chosen so typical sizes land in a
  // realistic small-to-moderate range with an occasional large outlier.
  std::lognormal_distribution<double> size_dist{3.0, 0.9};

  // General-purpose uniform draw, used for regime transition rolls and
  // burst-direction/magnitude selection.
  std::uniform_real_distribution<double> unit{0.0, 1.0};

  // Orders per tick, regime-dependent.
  std::poisson_distribution<int> quiet_arrivals{2};
  std::poisson_distribution<int> burst_arrivals{8};

  // Order type mix: most flow is limit orders.
  std::bernoulli_distribution is_market{0.08};

  // Cancellation: fraction of NEW limit orders get scheduled for a later
  // cancel, at a randomly chosen delay.
  std::bernoulli_distribution will_cancel{0.35};
  std::uniform_int_distribution<int> cancel_delay{5, 200};
};

// Regime transition probabilities (per tick).
constexpr double QUIET_TO_BURST = 0.01; // ~1 in 100 ticks
constexpr double BURST_TO_QUIET = 0.05; // bursts last ~20 ticks on avg

// ---------------------------------------------------------------------
// Output formatting
// ---------------------------------------------------------------------

void emit_new(std::ofstream &out, OrderId id, bool buy, bool market,
              Price price, Qty qty, std::uint64_t ts) {
  out << "NEW," << id << ',' << (buy ? "BUY" : "SELL") << ','
      << (market ? "MARKET" : "LIMIT") << ',';
  if (!market) {
    out << price;
  }
  out << ',' << qty << ',' << ts << '\n';
}

void emit_cancel(std::ofstream &out, OrderId id, std::uint64_t ts) {
  out << "CANCEL," << id << ",,,," << ',' << ts << '\n';
}

// ---------------------------------------------------------------------
// Per-tick phases
// ---------------------------------------------------------------------

// Maybe flip Quiet <-> Burst this tick, and if entering Burst, pick a
// fresh momentum direction/magnitude for side_bias.
void update_regime(SimState &state, Distributions &dist) {
  if (state.regime == Regime::Quiet) {
    if (dist.unit(state.rng) < QUIET_TO_BURST) {
      state.regime = Regime::Burst;
      state.side_bias = (dist.unit(state.rng) < 0.5 ? -1.0 : 1.0) *
                        (0.2 + 0.3 * dist.unit(state.rng));
    }
  } else {
    if (dist.unit(state.rng) < BURST_TO_QUIET) {
      state.regime = Regime::Quiet;
      state.side_bias = 0.0;
    }
  }
}

// One random-walk step for mid_price, nudged by side_bias if we're in a
// directional burst. Must run after update_regime (reads the fresh
// side_bias) and before generate_arrivals (which prices off mid_price).
void update_mid_price(SimState &state, Distributions &dist) {
  state.mid_price += dist.mid_walk(state.rng) + state.side_bias * 0.8;
  state.mid_price = std::max(state.mid_price, 100.0); // keep prices positive
}

// Drain every cancel scheduled for exactly this tick. Order-independent
// relative to the other phases -- it only touches ids from past ticks.
void fire_due_cancels(SimState &state, std::ofstream &out, std::uint64_t tick) {
  while (!state.pending_cancels.empty() &&
         state.pending_cancels.top().fire_at_tick == tick) {
    emit_cancel(out, state.pending_cancels.top().id, state.timestamp++);
    state.pending_cancels.pop();
  }
}

// Generate this tick's new order arrivals: how many, then for each one,
// side/type/size/price, emit it, and maybe schedule a future cancel.
void generate_arrivals(SimState &state, Distributions &dist, std::ofstream &out,
                       std::uint64_t tick) {
  int n_arrivals = (state.regime == Regime::Quiet)
                       ? dist.quiet_arrivals(state.rng)
                       : dist.burst_arrivals(state.rng);

  double p_buy = 0.5 + state.side_bias * 0.5; // bias skews buy/sell split
  p_buy = std::clamp(p_buy, 0.05, 0.95);
  std::bernoulli_distribution buy_dist(p_buy);

  for (int i = 0; i < n_arrivals; ++i) {
    OrderId id = state.next_id++;
    bool buy = buy_dist(state.rng);
    bool market = dist.is_market(state.rng);

    Qty qty =
        static_cast<Qty>(std::max(1.0, std::round(dist.size_dist(state.rng))));

    Price price = 0;
    if (!market) {
      Price offset = sample_laplace_offset(state.rng, /*scale=*/4.0);
      // Bias resting limit orders to sit on their own side of the mid
      // (buys below mid, sells above mid) so the book doesn't self-cross
      // on arrival more than realistically expected.
      Price signed_offset = buy ? -std::abs(offset) : std::abs(offset);
      price = static_cast<Price>(std::llround(state.mid_price)) + signed_offset;
      price = std::max<Price>(price, 1);
    }

    emit_new(out, id, buy, market, price, qty, state.timestamp++);

    if (!market && dist.will_cancel(state.rng)) {
      std::uint64_t fire_at =
          tick + static_cast<std::uint64_t>(dist.cancel_delay(state.rng));
      state.pending_cancels.push(PendingCancel{id, fire_at});
    }
  }
}

// Flush any cancels still pending past the last tick (their fire_at_tick
// exceeded num_ticks), so scheduled cancels aren't silently dropped from
// the dataset. The heap pops them in fire_at_tick order.
void flush_remaining_cancels(SimState &state, std::ofstream &out) {
  while (!state.pending_cancels.empty()) {
    emit_cancel(out, state.pending_cancels.top().id, state.timestamp++);
    state.pending_cancels.pop();
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <num_ticks> <output.csv> [seed]\n";
    return 1;
  }

  const std::uint64_t num_ticks = std::stoull(argv[1]);
  const std::string output_path = argv[2];
  const std::uint64_t seed = (argc >= 4) ? std::stoull(argv[3]) : 42ULL;

  std::ofstream out(output_path);
  if (!out) {
    std::cerr << "Failed to open output file: " << output_path << "\n";
    return 1;
  }
  out << "op,id,side,type,price,qty,timestamp\n";

  SimState state(seed);
  Distributions dist;

  for (std::uint64_t tick = 0; tick < num_ticks; ++tick) {
    update_regime(state, dist);
    update_mid_price(state, dist);
    fire_due_cancels(state, out, tick);
    generate_arrivals(state, dist, out, tick);
  }

  flush_remaining_cancels(state, out);

  std::cerr << "Generated " << (state.next_id - 1) << " orders across "
            << num_ticks << " ticks -> " << output_path << " (seed=" << seed
            << ")\n";
  return 0;
}