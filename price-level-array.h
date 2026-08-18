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
  size_t active_count;
  size_t lowest_active_index;
  size_t highest_active_index;
  static constexpr size_t npos = static_cast<size_t>(-1);

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

  size_t find_next_active(size_t start) const {
    if (start >= num_levels) {
      return npos;
    }

    size_t word_index = start / 64u;
    uint64_t word = bitmap[word_index] & (~0ULL << (start % 64u));
    while (true) {
      if (word != 0ULL) {
        size_t index = word_index * 64u +
                       static_cast<size_t>(__builtin_ctzll(word));
        return index < num_levels ? index : npos;
      }

      word_index++;
      if (word_index >= bitmap.size()) {
        return npos;
      }
      word = bitmap[word_index];
    }
  }

  size_t find_prev_active(size_t start) const {
    if (num_levels == 0) {
      return npos;
    }
    if (start >= num_levels) {
      start = num_levels - 1u;
    }

    size_t word_index = start / 64u;
    uint64_t word = bitmap[word_index] &
                    (~0ULL >> (63u - static_cast<unsigned>(start % 64u)));
    while (true) {
      if (word != 0ULL) {
        return word_index * 64u +
               static_cast<size_t>(63 - __builtin_clzll(word));
      }

      if (word_index == 0) {
        return npos;
      }
      word_index--;
      word = bitmap[word_index];
    }
  }

public:
  priceLevelArray(Price min_price, Price max_price, Price tick_size = 1)
      : min_price(min_price),
        max_price(max_price),
        tick_size(tick_size),
        num_levels(0),
        priceLevels(),
        bitmap(),
        active_count(0),
        lowest_active_index(npos),
        highest_active_index(npos) {
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
    bool was_active = is_active(index);
    bool now_active = priceLevels[index].order_count > 0;

    if (now_active) {
      mark_active(index);
      if (!was_active) {
        active_count++;
        if (lowest_active_index == npos || index < lowest_active_index) {
          lowest_active_index = index;
        }
        if (highest_active_index == npos || index > highest_active_index) {
          highest_active_index = index;
        }
      }
    } else {
      mark_inactive(index);
      if (was_active) {
        active_count--;
        if (active_count == 0) {
          lowest_active_index = npos;
          highest_active_index = npos;
        } else {
          if (index == lowest_active_index) {
            lowest_active_index = find_next_active(index + 1u);
          }
          if (index == highest_active_index) {
            highest_active_index =
                index == 0 ? npos : find_prev_active(index - 1u);
          }
        }
      }
    }
  }

  std::optional<Price> best_bid() const {
    if (highest_active_index == npos) {
      return std::nullopt;
    }

    return priceLevels[highest_active_index].price;
  }

  std::optional<Price> best_ask() const {
    if (lowest_active_index == npos) {
      return std::nullopt;
    }

    return priceLevels[lowest_active_index].price;
  }

  Price get_min_price() const { return min_price; }
  Price get_max_price() const { return max_price; }
  Price get_tick_size() const { return tick_size; }
  size_t size() const { return num_levels; }
};

#endif  // PRICE_LEVEL_ARRAY_H
