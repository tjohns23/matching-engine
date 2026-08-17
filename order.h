#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <limits>

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Qty = std::int64_t;
using PoolIndex = std::uint32_t;
inline constexpr PoolIndex kInvalidIndex =
    (std::numeric_limits<PoolIndex>::max)();

enum class Side : std::uint8_t { Buy, Sell };
enum class Type : std::uint8_t { Limit, Market };

struct Order {
  OrderId id;
  Side side;
  Price price;
  Qty remaining_qty;
  Type type;
  std::uint64_t sequence;
  std::uint32_t next;
  std::uint32_t prev;
};

#endif