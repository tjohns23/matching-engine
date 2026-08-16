#!/usr/bin/env python3
"""
generate_market_data.py

Produces a deterministic, reproducible synthetic order-flow dataset for
replay against OrderBook in simulation.cpp. This is a one-off tool (run
once, produce a CSV, done) so it's written in Python for readability
rather than C++ -- the generator's implementation language is invisible
to everything downstream, since simulation.cpp only ever reads the CSV.

Usage:
    python3 generate_market_data.py <num_ticks> <output.csv> [seed]

Output format (CSV, one line per operation):
    op,id,side,type,price,qty,tick,sequence
        op        NEW | CANCEL
        id        order id
        side      BUY | SELL          (empty for CANCEL)
        type      LIMIT | MARKET      (empty for CANCEL)
        price     integer price       (empty for CANCEL, empty for MARKET NEW)
        qty       integer quantity    (empty for CANCEL)
        tick      the simulation tick this operation was generated in.
                  Multiple rows can share a tick (a burst tick emits many
                  orders at once); ticks can also be skipped entirely (a
                  quiet tick can emit zero orders). This is what carries
                  the regime/clustering signal.
        sequence  monotonically increasing row counter, unique per row.
                  Used only for tie-breaking; replay order is already
                  determined by file order, not by this column.

Design notes:
    - Price offsets drawn from a Laplace distribution around a slowly
      random-walking mid price -> realistic clustering near the touch
      with fat tails, rather than uniform price selection.
    - Order sizes drawn from a log-normal distribution -> many small
      orders, occasional large ones, matching real order-size skew.
    - A two-state Markov chain (QUIET / BURST) drives arrival intensity
      and side imbalance over time, producing bursty, directionally-
      biased flow instead of a flat, balanced Poisson process.
    - A fraction of resting limit orders are scheduled for cancellation
      some number of ticks after insertion, so CANCEL and the
      empty-price-level cleanup path both get realistic exercise
      downstream. Scheduled cancels are kept in a min-heap ordered by
      fire_at_tick (not a plain FIFO queue) since the delay is
      randomized -- a far-future cancel can be scheduled before a
      near-future one, so only a priority queue guarantees the
      soonest-due cancel always fires first.
"""

import csv
import heapq
import sys
from dataclasses import dataclass, field
from enum import Enum, auto


class Regime(Enum):
    QUIET = auto()
    BURST = auto()


# Regime transition probabilities (per tick).
QUIET_TO_BURST = 0.01  # ~1 in 100 ticks
BURST_TO_QUIET = 0.05  # bursts last ~20 ticks on average


@dataclass
class SimState:
    rng: "numpy.random.Generator"
    mid_price: float = 10000.0          # integer-price ticks, e.g. cents
    regime: Regime = Regime.QUIET
    side_bias: float = 0.0              # -1..+1, skews P(buy) during bursts
    next_id: int = 1
    sequence: int = 0                   # monotonic row counter, not a time value
    pending_cancels: list = field(default_factory=list)  # min-heap: (fire_at_tick, id)


def update_regime(state: SimState) -> None:
    """Maybe flip QUIET <-> BURST this tick; entering BURST picks a fresh
    momentum direction/magnitude for side_bias."""
    if state.regime is Regime.QUIET:
        if state.rng.random() < QUIET_TO_BURST:
            state.regime = Regime.BURST
            direction = -1.0 if state.rng.random() < 0.5 else 1.0
            state.side_bias = direction * (0.2 + 0.3 * state.rng.random())
    else:
        if state.rng.random() < BURST_TO_QUIET:
            state.regime = Regime.QUIET
            state.side_bias = 0.0


def update_mid_price(state: SimState) -> None:
    """One random-walk step, nudged by side_bias during a directional burst.
    Must run after update_regime (reads the fresh side_bias) and before
    generate_arrivals (which prices orders off mid_price)."""
    step = state.rng.normal(0.0, 1.5)
    state.mid_price += step + state.side_bias * 0.8
    state.mid_price = max(state.mid_price, 100.0)  # keep prices positive


def fire_due_cancels(state: SimState, writer, tick: int) -> None:
    """Drain every cancel scheduled for exactly this tick."""
    while state.pending_cancels and state.pending_cancels[0][0] == tick:
        _, order_id = heapq.heappop(state.pending_cancels)
        writer.writerow(["CANCEL", order_id, "", "", "", "", tick, state.sequence])
        state.sequence += 1


def generate_arrivals(state: SimState, writer, tick: int) -> None:
    """Generate this tick's new order arrivals: how many, then for each
    one, side/type/size/price, emit it, and maybe schedule a future
    cancel."""
    mean_arrivals = 2 if state.regime is Regime.QUIET else 8
    n_arrivals = state.rng.poisson(mean_arrivals)

    p_buy = min(max(0.5 + state.side_bias * 0.5, 0.05), 0.95)

    for _ in range(n_arrivals):
        order_id = state.next_id
        state.next_id += 1

        buy = state.rng.random() < p_buy
        market = state.rng.random() < 0.08  # ~8% of flow is market orders

        qty = max(1, round(state.rng.lognormal(3.0, 0.9)))

        if market:
            side = "BUY" if buy else "SELL"
            writer.writerow(["NEW", order_id, side, "MARKET", "", qty, tick, state.sequence])
            state.sequence += 1
            continue

        offset = state.rng.laplace(0.0, 4.0)
        # Bias resting limit orders to sit on their own side of the mid
        # (buys below mid, sells above mid) so the book doesn't
        # self-cross on arrival more than realistically expected.
        signed_offset = -abs(offset) if buy else abs(offset)
        price = max(1, round(state.mid_price) + round(signed_offset))

        side = "BUY" if buy else "SELL"
        writer.writerow(["NEW", order_id, side, "LIMIT", price, qty, tick, state.sequence])
        state.sequence += 1

        if state.rng.random() < 0.35:  # ~35% of limit orders get a scheduled cancel
            delay = state.rng.integers(5, 201)  # [5, 200] ticks later
            heapq.heappush(state.pending_cancels, (tick + delay, order_id))


def flush_remaining_cancels(state: SimState, writer) -> None:
    """Flush any cancels still pending past the last tick, so scheduled
    cancels aren't silently dropped from the dataset. The heap pops them
    in fire_at_tick order."""
    while state.pending_cancels:
        fire_at_tick, order_id = heapq.heappop(state.pending_cancels)
        writer.writerow(["CANCEL", order_id, "", "", "", "", fire_at_tick, state.sequence])
        state.sequence += 1


def main() -> int:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <num_ticks> <output.csv> [seed]", file=sys.stderr)
        return 1

    num_ticks = int(sys.argv[1])
    output_path = sys.argv[2]
    seed = int(sys.argv[3]) if len(sys.argv) >= 4 else 42

    import numpy as np  # local import so --help-style usage errors don't require numpy

    state = SimState(rng=np.random.default_rng(seed))

    with open(output_path, "w", newline="") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["op", "id", "side", "type", "price", "qty", "tick", "sequence"])

        for tick in range(num_ticks):
            update_regime(state)
            update_mid_price(state)
            fire_due_cancels(state, writer, tick)
            generate_arrivals(state, writer, tick)

        flush_remaining_cancels(state, writer)

    print(f"Generated {state.next_id - 1} orders across {num_ticks} ticks "
          f"-> {output_path} (seed={seed})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())