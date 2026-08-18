// simulation.cpp
//
// Replays a market-data CSV (produced by generate_market_data) through
// OrderBook. Two modes, chosen at the command line:
//
//   invariant   Runs correctness checks after every operation. Slower, but
//               catches bugs a fixed set of hand-written unit tests would
//               miss, since the operation sequence is large and randomized.
//
//   perf        Raw replay, no checks, no extra bookkeeping. This is what
//               you point `perf stat` / `perf record` at.
//
// Usage:
//   ./simulation invariant market_data.csv
//   ./simulation perf market_data.csv
//
// Both modes read the exact same dataset, so a bug the invariant mode
// finds is reproducible, and perf numbers are comparable across runs.

#include "lazycsv.hpp" // Ensure lazycsv.hpp is in your include path
#include "order-book.h"
#include "order.h"

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------
// One row of the input CSV, already parsed into real types.
// ---------------------------------------------------------------------
enum class OpKind { New, Cancel };

struct Operation {
  OpKind kind;
  Order order;        // fully populated for New; only order.id used for Cancel
  std::uint64_t tick; // the real simulation tick this op belongs to
  std::uint64_t sequence; // unique row counter, used for total ordering
};

// ---------------------------------------------------------------------
// CSV parsing (Zero-Allocation via lazycsv + charconv)
// ---------------------------------------------------------------------

Side parse_side(std::string_view text) {
  return text == "BUY" ? Side::Buy : Side::Sell;
}

Type parse_type(std::string_view text) {
  return text == "MARKET" ? Type::Market : Type::Limit;
}

// Highly optimized integer parsing that reads directly from the memory-mapped
// file.
template <typename T> T parse_int_or_zero(std::string_view text) {
  if (text.empty())
    return 0;
  T value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

std::vector<Operation> read_operations(const std::string &csv_path) {
  std::vector<Operation> operations;

  operations.reserve(40000000);

  // lazycsv defaults to mmap under the hood
  lazycsv::parser parser{csv_path};

  bool is_header = true;
  // Skip the first row
  for (const auto row : parser) {
    if (is_header) {
      is_header = false;
      continue;
    }

    // Safely extract up to 8 cells per row into string views
    std::array<std::string_view, 8> fields;
    size_t col = 0;
    for (const auto cell : row) {
      if (col < 8) {
        fields[col] = cell.trimmed();
      }
      col++;
    }

    // Skip empty lines at EOF
    if (col == 0)
      continue;

    // Pad any missing columns at the end of a row (e.g., short CANCEL rows)
    for (; col < 8; ++col) { // This picks up where col left off
      fields[col] = "";
    }

    std::string_view op_view = fields[0];
    OrderId id = parse_int_or_zero<OrderId>(fields[1]);
    std::uint64_t tick = parse_int_or_zero<std::uint64_t>(fields[6]);
    std::uint64_t sequence = parse_int_or_zero<std::uint64_t>(fields[7]);

    if (op_view == "CANCEL") {
      Order order{};
      order.id = id;
      operations.push_back(Operation{OpKind::Cancel, order, tick, sequence});
      continue;
    }

    Order order{};
    order.id = id;
    order.side = parse_side(fields[2]);
    order.type = parse_type(fields[3]);
    order.price = parse_int_or_zero<std::int64_t>(fields[4]);
    order.remaining_qty = parse_int_or_zero<std::int64_t>(fields[5]);
    order.sequence = sequence;
    operations.push_back(Operation{OpKind::New, order, tick, sequence});
  }

  return operations;
}

// ---------------------------------------------------------------------
// Invariant checking
// ---------------------------------------------------------------------

class InvariantChecker {
public:
  void record_new_order(const Order &submitted_order) {
    original_qty[submitted_order.id] = submitted_order.remaining_qty;
    if (submitted_order.type == Type::Limit) {
      original_price[submitted_order.id] = submitted_order.price;
    }
  }

  void check(const OrderBook &book, std::uint64_t tick) {
    check_book_never_crosses(book, tick);
    check_new_trades_are_consistent(book, tick);
  }

private:
  void check_book_never_crosses(const OrderBook &book, std::uint64_t tick) {
    auto bid = book.best_bid();
    auto ask = book.best_ask();
    if (bid && ask && *bid >= *ask) {
      fail(tick, "book crossed: best_bid=" + std::to_string(*bid) +
                     " best_ask=" + std::to_string(*ask));
    }
  }

  void check_new_trades_are_consistent(const OrderBook &book,
                                       std::uint64_t tick) {
    const auto &trades = book.trades();
    for (std::size_t i = trades_seen; i < trades.size(); ++i) {
      const Trade &trade = trades[i];

      if (trade.qty <= 0) {
        fail(tick, "trade with non-positive qty for maker " +
                       std::to_string(trade.maker_order_id));
      }
      if (!original_qty.count(trade.maker_order_id)) {
        fail(tick, "trade references unknown maker id " +
                       std::to_string(trade.maker_order_id));
      }
      if (!original_qty.count(trade.taker_order_id)) {
        fail(tick, "trade references unknown taker id " +
                       std::to_string(trade.taker_order_id));
      }

      auto maker_price_it = original_price.find(trade.maker_order_id);
      if (maker_price_it != original_price.end() &&
          trade.price != maker_price_it->second) {
        fail(tick, "trade price " + std::to_string(trade.price) +
                       " does not match maker " +
                       std::to_string(trade.maker_order_id) +
                       "'s resting price " +
                       std::to_string(maker_price_it->second));
      }

      filled_qty[trade.maker_order_id] += trade.qty;
      filled_qty[trade.taker_order_id] += trade.qty;

      Qty filled = filled_qty[trade.maker_order_id];
      Qty started = original_qty[trade.maker_order_id];
      if (filled > started) {
        fail(tick, "order " + std::to_string(trade.maker_order_id) +
                       " overfilled: filled=" + std::to_string(filled) +
                       " started=" + std::to_string(started));
      }
    }
    trades_seen = trades.size();
  }

  [[noreturn]] void fail(std::uint64_t tick, const std::string &reason) {
    throw std::logic_error("Invariant violated at tick " +
                           std::to_string(tick) + ": " + reason);
  }

  std::unordered_map<OrderId, Qty> original_qty;
  std::unordered_map<OrderId, Price> original_price;
  std::unordered_map<OrderId, Qty> filled_qty;
  std::size_t trades_seen = 0;
};

// ---------------------------------------------------------------------
// Replay loops
// ---------------------------------------------------------------------

void apply_operation(OrderBook &book, Operation &operation) {
  if (operation.kind == OpKind::New) {
    book.submit_order(operation.order);
  } else {
    book.cancel_order(operation.order.id);
  }
}

class LoopPerfCounters {
public:
  void start() {
#if defined(__linux__)
    open_counter(cache_refs_fd, PERF_TYPE_HARDWARE,
                 PERF_COUNT_HW_CACHE_REFERENCES);
    open_counter(cache_misses_fd, PERF_TYPE_HARDWARE,
                 PERF_COUNT_HW_CACHE_MISSES);
    open_counter(l1_loads_fd, PERF_TYPE_HW_CACHE,
                 PERF_COUNT_HW_CACHE_L1D |
                     (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                     (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
    open_counter(l1_load_misses_fd, PERF_TYPE_HW_CACHE,
                 PERF_COUNT_HW_CACHE_L1D |
                     (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                     (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));

    reset_and_enable(cache_refs_fd);
    reset_and_enable(cache_misses_fd);
    reset_and_enable(l1_loads_fd);
    reset_and_enable(l1_load_misses_fd);
#endif
  }

  void stop_and_print() {
#if defined(__linux__)
    disable(cache_refs_fd);
    disable(cache_misses_fd);
    disable(l1_loads_fd);
    disable(l1_load_misses_fd);

    std::uint64_t cache_refs = read_counter(cache_refs_fd);
    std::uint64_t cache_misses = read_counter(cache_misses_fd);
    std::uint64_t l1_loads = read_counter(l1_loads_fd);
    std::uint64_t l1_load_misses = read_counter(l1_load_misses_fd);

    close_counter(cache_refs_fd);
    close_counter(cache_misses_fd);
    close_counter(l1_loads_fd);
    close_counter(l1_load_misses_fd);

    if (cache_refs > 0) {
      double miss_rate = static_cast<double>(cache_misses) /
                         static_cast<double>(cache_refs) * 100.0;
      std::cout << "engine cache misses: " << cache_misses << " / "
                << cache_refs << " (" << miss_rate << "%)\n";
    }
    if (l1_loads > 0) {
      double miss_rate = static_cast<double>(l1_load_misses) /
                         static_cast<double>(l1_loads) * 100.0;
      std::cout << "engine L1D load misses: " << l1_load_misses << " / "
                << l1_loads << " (" << miss_rate << "%)\n";
    }
#endif
  }

private:
#if defined(__linux__)
  int cache_refs_fd = -1;
  int cache_misses_fd = -1;
  int l1_loads_fd = -1;
  int l1_load_misses_fd = -1;

  static long perf_event_open(perf_event_attr *attr, pid_t pid, int cpu,
                              int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
  }

  static void open_counter(int &fd, std::uint32_t type, std::uint64_t config) {
    perf_event_attr attr{};
    attr.type = type;
    attr.size = sizeof(attr);
    attr.config = config;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    fd = static_cast<int>(perf_event_open(&attr, 0, -1, -1, 0));
  }

  static void reset_and_enable(int fd) {
    if (fd >= 0) {
      ioctl(fd, PERF_EVENT_IOC_RESET, 0);
      ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }
  }

  static void disable(int fd) {
    if (fd >= 0) {
      ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    }
  }

  static std::uint64_t read_counter(int fd) {
    std::uint64_t value = 0;
    if (fd >= 0) {
      ssize_t bytes = read(fd, &value, sizeof(value));
      if (bytes != static_cast<ssize_t>(sizeof(value))) {
        return 0;
      }
    }
    return value;
  }

  static void close_counter(int &fd) {
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
#endif
};

// Ensure invariants aren't violated
void run_invariant_mode(std::vector<Operation> &operations) {
  OrderBook book;
  InvariantChecker checker;

  for (Operation &operation : operations) {
    if (operation.kind == OpKind::New) {
      checker.record_new_order(operation.order);
    }
    apply_operation(book, operation);
    checker.check(book, operation.tick);
  }

  std::cout << "invariant mode: " << operations.size()
            << " operations replayed, no violations found\n";
}

void run_perf_mode(std::vector<Operation> &operations) {
  OrderBook book;
  LoopPerfCounters counters;

  // Isolate strictly the engine execution loop
  counters.start();
  auto start = std::chrono::steady_clock::now();
  for (Operation &operation : operations) {
    apply_operation(book, operation);
  }
  auto end = std::chrono::steady_clock::now();
  counters.stop_and_print();

  double seconds = std::chrono::duration<double>(end - start).count();
  double ops_per_second = operations.size() / seconds;

  std::cout << "perf mode: " << operations.size() << " operations in "
            << seconds << "s (" << static_cast<std::uint64_t>(ops_per_second)
            << " ops/sec)\n";
}

// ---------------------------------------------------------------------
int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0]
              << " <invariant|perf> <market_data.csv>\n";
    return 1;
  }

  std::string mode = argv[1];
  std::string csv_path = argv[2];

  std::vector<Operation> operations;
  try {
    operations = read_operations(csv_path);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load market data: " << e.what() << "\n";
    return 1;
  }

  try {
    if (mode == "invariant") {
      run_invariant_mode(operations);
    } else if (mode == "perf") {
      run_perf_mode(operations);
    } else {
      std::cerr << "Unknown mode: " << mode
                << " (expected invariant or perf)\n";
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
