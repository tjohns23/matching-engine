#include "order-book.h"
#include "order.h"
#include <iostream>
#include <stdexcept>

bool SHOULD_LOG = false;

void OrderBook::push_back(PriceLevel &level, PoolIndex index) {
  Order &order = pool[index];
  order.prev = level.tail;
  order.next = kInvalidIndex;

  if (level.tail != kInvalidIndex) {
    pool[level.tail].next = index;
  } else {
    level.head = index;
  }

  level.tail = index;
  level.total_qty += order.remaining_qty;
  level.order_count++;
  level.price = order.price;
}

void OrderBook::unlink(PriceLevel &level, PoolIndex index) {
  Order &order = pool[index];

  if (order.prev != kInvalidIndex) {
    pool[order.prev].next = order.next;
  } else {
    level.head = order.next;
  }

  if (order.next != kInvalidIndex) {
    pool[order.next].prev = order.prev;
  } else {
    level.tail = order.prev;
  }

  level.total_qty -= order.remaining_qty;
  level.order_count--;
  order.prev = kInvalidIndex;
  order.next = kInvalidIndex;
}

void OrderBook::add_order(const Order &order) {
  PoolIndex index = pool.add_pool_order(order);

  switch (order.side) {
  case Side::Buy: {
    PriceLevel &price_list = bids.get_level(order.price);
    price_list.price = order.price;
    push_back(price_list, index);
    bids.on_level_updated(order.price);
    order_map[order.id] = index;
    break;
  }

  case Side::Sell: {
    PriceLevel &price_list = asks.get_level(order.price);
    price_list.price = order.price;
    push_back(price_list, index);
    asks.on_level_updated(order.price);
    order_map[order.id] = index;
    break;
  }
  }
}

bool OrderBook::remove_order(OrderId id) {
  auto order_map_it = order_map.find(id);
  if (order_map_it == order_map.end()) {
    return false;
  }

  PoolIndex index = order_map_it->second;
  Side side = pool[index].side;
  Price price = pool[index].price;

  switch (side) {
  case Side::Buy: {
    if (bids.has_level(price)) {
      PriceLevel &level = bids.get_level(price);
      unlink(level, index);
      bids.on_level_updated(price);
    }
    break;
  }

  case Side::Sell: {
    if (asks.has_level(price)) {
      PriceLevel &level = asks.get_level(price);
      unlink(level, index);
      asks.on_level_updated(price);
    }
    break;
  }
  }

  pool.remove_pool_order(index);
  order_map.erase(order_map_it);

  return true;
}

void OrderBook::cancel_order(OrderId id) {

  // Remove the order
  bool was_removed = remove_order(id);
  if (SHOULD_LOG) {
    if (was_removed) {
      events.push_back("[LOG] Order " + std::to_string(id) +
                       " successfully canceled.");
    } else {
      events.push_back("[LOG] Cancel failed: Order " + std::to_string(id) +
                       " could not be found ");
    }
  }
}

void OrderBook::match_buy(Order &order) {
  switch (order.type) {
  case Type::Market: {
    while (order.remaining_qty > 0) {
      auto best = asks.best_ask();
      if (!best.has_value()) {
        break;
      }

      PriceLevel &level = asks.get_level(best.value());
      Order &resting = pool[level.head];

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;
      asks.on_level_updated(best.value());
      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }
    if (SHOULD_LOG) {
      if (order.remaining_qty > 0) {
        events.push_back("[LOG] Market buy order " + std::to_string(order.id) +
                         " failed (insufficient asks)");
      } else {
        events.push_back("[LOG] Market buy order " + std::to_string(order.id) +
                         " successfully filled.");
      }
    }
    break;
  }

  case Type::Limit: {
    while (order.remaining_qty > 0) {
      auto best = asks.best_ask();
      if (!best.has_value()) {
        break;
      }

      Price best_ask = best.value();
      if (best_ask > order.price) {
        break;
      }

      PriceLevel &level = asks.get_level(best_ask);
      Order &resting = pool[level.head];

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;
      asks.on_level_updated(best_ask);
      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }

    if (SHOULD_LOG && order.remaining_qty == 0) {
      events.push_back("[LOG] Limit buy order " + std::to_string(order.id) +
                       " successfully filled.");
    }

    break;
  }
  }
}

void OrderBook::match_sell(Order &order) {
  switch (order.type) {
  case Type::Market: {
    while (order.remaining_qty > 0) {
      auto best = bids.best_bid();
      if (!best.has_value()) {
        break;
      }

      PriceLevel &level = bids.get_level(best.value());
      Order &resting = pool[level.head];

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;
      bids.on_level_updated(best.value());
      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }
    if (SHOULD_LOG) {
      if (order.remaining_qty > 0) {
        events.push_back("[LOG] Market sell order " + std::to_string(order.id) +
                         " failed (insufficient bids)");
      } else {
        events.push_back("[LOG] Market sell order " + std::to_string(order.id) +
                         " successfully filled.");
      }
    }
    break;
  }

  case Type::Limit: {
    while (order.remaining_qty > 0) {
      auto best = bids.best_bid();
      if (!best.has_value()) {
        break;
      }

      Price best_bid = best.value();
      if (best_bid < order.price) {
        break;
      }

      PriceLevel &level = bids.get_level(best_bid);
      Order &resting = pool[level.head];

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;
      bids.on_level_updated(best_bid);
      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }

    if (SHOULD_LOG && order.remaining_qty == 0) {
      events.push_back("[LOG] Limit sell order " + std::to_string(order.id) +
                       " successfully filled.");
    }

    break;
  }
  }
}

void OrderBook::submit_order(Order &order) {
  switch (order.side) {
  case Side::Buy:
    match_buy(order);
    break;
  case Side::Sell:
    match_sell(order);
    break;
  }

  if (order.remaining_qty > 0) {
    if (order.type == Type::Limit) {
      add_order(order);
      if (SHOULD_LOG) {
        events.push_back("[LOG] Limit order " + std::to_string(order.id) +
                         " successfully added to book.");
      }
    } else {
      if (SHOULD_LOG) {
        events.push_back("[LOG] Market Order " + std::to_string(order.id) +
                         " could not be fully filled. Unfilled quantity: " +
                         std::to_string(order.remaining_qty));
      }
    }
  }
}

/******************************
 *         Accessors          *
 ******************************/

// ---- Existence & lookup ----

bool OrderBook::has_order(OrderId id) const {
  return order_map.find(id) != order_map.end();
}

bool OrderBook::has_price_level(Side side, Price price) const {
  switch (side) {
  case Side::Buy:
    return bids.has_level(price);
  case Side::Sell:
    return asks.has_level(price);
  }
  return false;
}

// ---- Per-order state ----

Qty OrderBook::remaining_qty_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("remaining_qty_of: no such order id");
  }
  return pool[order_it->second].remaining_qty;
}

Side OrderBook::side_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("side_of: no such order id");
  }
  return pool[order_it->second].side;
}

Price OrderBook::price_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("price_of: no such order id");
  }
  return pool[order_it->second].price;
}

// ---- Top of book ----

std::optional<Price> OrderBook::best_bid() const {
  return bids.best_bid();
}

std::optional<Price> OrderBook::best_ask() const {
  return asks.best_ask();
}

// ---- Per-level aggregates ----

Qty OrderBook::level_qty(Side side, Price price) const {
  if (side == Side::Buy) {
    if (!bids.has_level(price)) {
      return 0;
    }
    return bids.get_level(price).total_qty;
  }

  if (!asks.has_level(price)) {
    return 0;
  }
  return asks.get_level(price).total_qty;
}

size_t OrderBook::level_order_count(Side side, Price price) const {
  if (side == Side::Buy) {
    if (!bids.has_level(price)) {
      return 0;
    }
    return bids.get_level(price).order_count;
  }

  if (!asks.has_level(price)) {
    return 0;
  }
  return asks.get_level(price).order_count;
}

OrderId OrderBook::front_order_id(Side side, Price price) const {
  if (side == Side::Buy) {
    if (!bids.has_level(price)) {
      throw std::out_of_range("front_order_id: no such price level");
    }
    const PriceLevel &level = bids.get_level(price);
    if (level.head == kInvalidIndex) {
      throw std::out_of_range("front_order_id: no such price level");
    }
    return pool[level.head].id;
  }

  if (!asks.has_level(price)) {
    throw std::out_of_range("front_order_id: no such price level");
  }
  const PriceLevel &level = asks.get_level(price);
  if (level.head == kInvalidIndex) {
    throw std::out_of_range("front_order_id: no such price level");
  }
  return pool[level.head].id;
}

// ---- Book-wide counts ----

size_t OrderBook::bid_count() const {
  size_t count = 0;
  for (Price price = bids.get_min_price(); price <= bids.get_max_price();
       price += bids.get_tick_size()) {
    if (bids.has_level(price)) {
      count += bids.get_level(price).order_count;
    }
  }
  return count;
}

size_t OrderBook::ask_count() const {
  size_t count = 0;
  for (Price price = asks.get_min_price(); price <= asks.get_max_price();
       price += asks.get_tick_size()) {
    if (asks.has_level(price)) {
      count += asks.get_level(price).order_count;
    }
  }
  return count;
}

// ---- Trade log ----

const std::vector<Trade> &OrderBook::trades() const { return last_trades; }

// ---- Event log ----

const std::vector<std::string> &OrderBook::get_events() const { return events; }