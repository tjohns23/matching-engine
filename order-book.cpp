#include "order-book.h"
#include "order.h"
#include <iostream>
#include <stdexcept>

void OrderBook::add_order(const Order &order) {

  switch (order.side) {

  case Side::Buy: {
    PriceLevel &price_list = bids[order.price];
    price_list.orders.push_back(order);

    // Keep track of the iterator to the inserted element
    auto order_it = std::prev(price_list.orders.end());
    order_map[order.id] = order_it;
    price_list.total_qty += order.remaining_qty;
    break;
  }

  case Side::Sell: {
    PriceLevel &price_list = asks[order.price];
    price_list.orders.push_back(order);

    // Keep track of the iterator to the inserted element
    auto order_it = std::prev(price_list.orders.end());
    order_map[order.id] = order_it;
    price_list.total_qty += order.remaining_qty;
    break;
  }
  }
}

bool OrderBook::remove_order(OrderId id) {
  // Check if the order exists
  auto order_map_it = order_map.find(id);
  if (order_map_it == order_map.end()) {
    return false;
  }

  auto order_it = order_map_it->second;
  Side side = order_it->side;
  Price price = order_it->price;

  switch (side) {
  case Side::Buy: {
    auto price_it = bids.find(price);
    if (price_it != bids.end()) {
      price_it->second.total_qty -= order_it->remaining_qty;
      price_it->second.orders.erase(order_it);
      if (price_it->second.orders.empty()) {
        bids.erase(price_it);
      }
    }
    break;
  }

  case Side::Sell: {
    auto price_it = asks.find(price);
    if (price_it != asks.end()) {
      price_it->second.total_qty -= order_it->remaining_qty;
      price_it->second.orders.erase(order_it);
      if (price_it->second.orders.empty()) {
        asks.erase(price_it);
      }
    }
    break;
  }
  }

  order_map.erase(order_map_it);

  return true;
}

void OrderBook::cancel_order(OrderId id) {

  // Remove the order
  bool was_removed = remove_order(id);
  if (was_removed) {
    events.push_back("[LOG] Order " + std::to_string(id) +
                     " successfully canceled.");
  } else {
    events.push_back("[LOG] Cancel failed: Order " + std::to_string(id) +
                     " could not be found ");
  }
}

void OrderBook::match_buy(Order &order) {
  switch (order.type) {
  case Type::Market: {
    while (order.remaining_qty > 0 && !asks.empty()) {
      auto best = asks.begin();
      auto &level = best->second;
      auto &resting = level.orders.front();

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;

      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }
    if (order.remaining_qty > 0) {
      events.push_back("[LOG] Market buy order " + std::to_string(order.id) +
                       " failed (insufficient asks)");
    } else {
      events.push_back("[LOG] Market buy order " + std::to_string(order.id) +
                       " successfully filled.");
    }
    break;
  }

  case Type::Limit: {
    while (order.remaining_qty > 0 && !asks.empty()) {
      auto best = asks.begin();
      Price best_ask = best->first;
      if (best_ask > order.price)
        break; // Best is too expensive

      auto &level = best->second;
      auto &resting = level.orders.front();

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;

      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }

    if (order.remaining_qty == 0) {
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
    while (order.remaining_qty > 0 && !bids.empty()) {
      auto best = bids.begin();
      auto &level = best->second;
      auto &resting = level.orders.front();

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;

      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }
    if (order.remaining_qty > 0) {
      events.push_back("[LOG] Market sell order " + std::to_string(order.id) +
                       " failed (insufficient bids)");
    } else {
      events.push_back("[LOG] Market sell order " + std::to_string(order.id) +
                       " successfully filled.");
    }
    break;
  }

  case Type::Limit: {
    while (order.remaining_qty > 0 && !bids.empty()) {
      auto best = bids.begin();
      Price best_bid = best->first;
      if (best_bid < order.price)
        break; // Best is too cheap

      auto &level = best->second;
      auto &resting = level.orders.front();

      Qty fill = std::min(order.remaining_qty, resting.remaining_qty);
      order.remaining_qty -= fill;
      resting.remaining_qty -= fill;
      level.total_qty -= fill;

      last_trades.push_back(Trade{resting.id, order.id, resting.price, fill});

      if (resting.remaining_qty == 0) {
        remove_order(resting.id);
      }
    }

    if (order.remaining_qty == 0) {
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
      events.push_back("[LOG] Limit order " + std::to_string(order.id) +
                       " successfully added to book.");
    } else {
      events.push_back("[LOG] Market Order " + std::to_string(order.id) +
                       " could not be fully filled. Unfilled quantity: " +
                       std::to_string(order.remaining_qty));
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
    return bids.find(price) != bids.end();
  case Side::Sell:
    return asks.find(price) != asks.end();
  }
  return false;
}

// ---- Per-order state ----

Qty OrderBook::remaining_qty_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("remaining_qty_of: no such order id");
  }
  return order_it->second->remaining_qty;
}

Side OrderBook::side_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("side_of: no such order id");
  }
  return order_it->second->side;
}

Price OrderBook::price_of(OrderId id) const {
  auto order_it = order_map.find(id);
  if (order_it == order_map.end()) {
    throw std::out_of_range("price_of: no such order id");
  }
  return order_it->second->price;
}

// ---- Top of book ----

std::optional<Price> OrderBook::best_bid() const {
  if (bids.empty()) {
    return std::nullopt;
  }
  return bids.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
  if (asks.empty()) {
    return std::nullopt;
  }
  return asks.begin()->first;
}

// ---- Per-level aggregates ----

Qty OrderBook::level_qty(Side side, Price price) const {
  switch (side) {
  case Side::Buy: {
    auto price_it = bids.find(price);
    return price_it != bids.end() ? price_it->second.total_qty : 0;
  }
  case Side::Sell: {
    auto price_it = asks.find(price);
    return price_it != asks.end() ? price_it->second.total_qty : 0;
  }
  }
  return 0;
}

size_t OrderBook::level_order_count(Side side, Price price) const {
  switch (side) {
  case Side::Buy: {
    auto price_it = bids.find(price);
    if (price_it == bids.end()) {
      return 0;
    }
    return price_it->second.orders.size();
  }
  case Side::Sell: {
    auto price_it = asks.find(price);
    if (price_it == asks.end()) {
      return 0;
    }
    return price_it->second.orders.size();
  }
  }
  return 0;
}

OrderId OrderBook::front_order_id(Side side, Price price) const {
  switch (side) {
  case Side::Buy: {
    auto price_it = bids.find(price);
    if (price_it == bids.end() || price_it->second.orders.empty()) {
      throw std::out_of_range("front_order_id: no such price level");
    }
    return price_it->second.orders.front().id;
  }
  case Side::Sell: {
    auto price_it = asks.find(price);
    if (price_it == asks.end() || price_it->second.orders.empty()) {
      throw std::out_of_range("front_order_id: no such price level");
    }
    return price_it->second.orders.front().id;
  }
  }
  throw std::invalid_argument("front_order_id: invalid side");
}

// ---- Book-wide counts ----

size_t OrderBook::bid_count() const {
  size_t count = 0;
  for (const auto &entry : bids) {
    count += entry.second.orders.size();
  }
  return count;
}

size_t OrderBook::ask_count() const {
  size_t count = 0;
  for (const auto &entry : asks) {
    count += entry.second.orders.size();
  }
  return count;
}

// ---- Trade log ----

const std::vector<Trade> &OrderBook::trades() const { return last_trades; }

// ---- Event log ----

const std::vector<std::string> &OrderBook::get_events() const { return events; }
