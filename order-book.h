#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "order.h"

struct Trade {
  OrderId maker_order_id;
  OrderId taker_order_id;
  Price price;
  Qty qty;
};

struct PriceLevel {
  Price price;
  Qty total_qty = 0;
  std::list<Order> orders;
};

class OrderBook {
private:
  std::map<Price, PriceLevel, std::greater<Price>> bids;
  std::map<Price, PriceLevel> asks;
  std::unordered_map<OrderId, std::list<Order>::iterator> order_map;
  std::vector<Trade> last_trades;
  std::vector<std::string> events;

  void add_order(const Order &order);
  bool remove_order(OrderId id);
  void match_buy(Order &order);
  void match_sell(Order &order);
  void match(Order &order);

public:
  void submit_order(Order &order);
  void cancel_order(OrderId id);

  // --- accessors for testing (and legitimate introspection) ---

  // Existence / lookup
  bool has_order(OrderId id) const;
  bool has_price_level(Side side, Price price) const;

  // Per-order state
  Qty remaining_qty_of(OrderId id) const;
  Side side_of(OrderId id) const;
  Price price_of(OrderId id) const;

  // Top of book
  std::optional<Price> best_bid() const;
  std::optional<Price> best_ask() const;

  // Per-level aggregates
  Qty level_qty(Side side, Price price) const;
  size_t level_order_count(Side side, Price price) const;
  OrderId front_order_id(Side side, Price price) const;

  // Book-wide counts
  size_t bid_count() const;
  size_t ask_count() const;

  // Trade log
  const std::vector<Trade> &trades() const;

  // Event log
  const std::vector<std::string> &get_events() const;
};

#endif
