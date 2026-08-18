#ifndef PRICE_LEVEL_ARRAY_H
#define PRICE_LEVEL_ARRAY_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "order-pool.h"
#include "order.h"

struct PriceLevel {
  Price price;
  Qty total_qty = 0;
  PoolIndex head = kInvalidIndex;
  PoolIndex tail = kInvalidIndex;
  std::uint32_t order_count = 0;
};

class priceLevelArray {
private:
  Price min_price;
  Price max_price;
  Price tick_size;
  size_t num_levels;
  std::vector<PriceLevel> priceLevels;
  std::vector<uint64_t> bitmap;

  inline size_t price_to_index(Price price) const {
    if (price < min_price || price > max_price) {
      throw std::out_of_range("Price out of bounded range in get_level()");
    }

    Price offset = price - min_price;
    if (offset % tick_size != 0) {
      throw std::invalid_argument("Price does not align to the configured tick size");
    }

    return static_cast<size_t>(offset / tick_size);
  }

  inline void mark_active(size_t index) {
    bitmap[index / 64] |= (1ULL << (index % 64));
  }

  inline void mark_inactive(size_t index) {
    bitmap[index / 64] &= ~(1ULL << (index % 64));
  }

  inline bool is_active(size_t index) const {
    return (bitmap[index / 64] & (1ULL << (index % 64))) != 0;
  }

public:
  priceLevelArray(Price min_price, Price max_price, Price tick_size = 1)
      : min_price(min_price),
        max_price(max_price),
        tick_size(tick_size),
        num_levels(0),
        priceLevels(),
        bitmap() {
    if (tick_size <= 0) {
      throw std::invalid_argument("tick_size must be positive");
    }
    if (max_price < min_price) {
      throw std::invalid_argument("max_price must be >= min_price");
    }

    const Price range = max_price - min_price;
    num_levels = static_cast<size_t>(range / tick_size) + 1u;

    priceLevels.resize(num_levels);
    bitmap.resize((num_levels + 63u) / 64u, 0ULL);

    for (size_t i = 0; i < num_levels; ++i) {
      priceLevels[i].price = min_price + static_cast<Price>(i) * tick_size;
    }
  }

  PriceLevel &get_level(Price price) {
    size_t idx = price_to_index(price);
    return priceLevels[idx];
  }

  const PriceLevel &get_level(Price price) const {
    size_t idx = price_to_index(price);
    return priceLevels[idx];
  }

  bool has_level(Price price) const {
    if (price < min_price || price > max_price) {
      return false;
    }

    Price offset = price - min_price;
    if (offset % tick_size != 0) {
      return false;
    }

    return is_active(static_cast<size_t>(offset / tick_size));
  }

  void on_level_updated(Price price) {
    if (price < min_price || price > max_price) {
      return;
    }

    Price offset = price - min_price;
    if (offset % tick_size != 0) {
      return;
    }

    size_t index = static_cast<size_t>(offset / tick_size);
    if (priceLevels[index].order_count > 0) {
      mark_active(index);
    } else {
      mark_inactive(index);
    }
  }

  std::optional<Price> best_bid() const {
    if (bitmap.empty()) {
      return std::nullopt;
    }

    for (int word_index = static_cast<int>(bitmap.size()) - 1; word_index >= 0;
         --word_index) {
      uint64_t word = bitmap[static_cast<size_t>(word_index)];
      if (word != 0ULL) {
        int bit_index = 63 - __builtin_clzll(word);
        size_t index = static_cast<size_t>(word_index) * 64u +
                       static_cast<size_t>(bit_index);
        return priceLevels[index].price;
      }
    }

    return std::nullopt;
  }

  std::optional<Price> best_ask() const {
    for (size_t word_index = 0; word_index < bitmap.size(); ++word_index) {
      uint64_t word = bitmap[word_index];
      if (word != 0ULL) {
        int bit_index = __builtin_ctzll(word);
        size_t index = word_index * 64u + static_cast<size_t>(bit_index);
        return priceLevels[index].price;
      }
    }

    return std::nullopt;
  }

  Price get_min_price() const { return min_price; }
  Price get_max_price() const { return max_price; }
  Price get_tick_size() const { return tick_size; }
  size_t size() const { return num_levels; }
};

#endif  // PRICE_LEVEL_ARRAY_H