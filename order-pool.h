#ifndef ORDER_POOL_H
#define ORDER_POOL_H

#include "order.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <vector>

class OrderPool {
public:
  // Constructor
  explicit OrderPool(std::size_t initial_capacity = 1024) {
    orders.resize(initial_capacity);
    initialize_free_list(0, initial_capacity);
    free_head = initial_capacity > 0 ? 0 : kInvalidIndex;
  }

  // Add an order
  PoolIndex add_pool_order(const Order &order) {
    return acquire_and_place(order);
  }

  // Remove an order
  void remove_pool_order(PoolIndex index) { release(index); }

  Order &operator[](PoolIndex index) {
    assert(index < orders.size());
    return orders[index];
  }

  const Order &operator[](PoolIndex index) const {
    assert(index < orders.size());
    return orders[index];
  }

  std::size_t capacity() const { return orders.size(); }

private:
  // Resizes the order pool
  void grow() {
    std::size_t old_size = orders.size();
    std::size_t new_size = old_size == 0 ? 1024 : old_size * 2;
    orders.resize(new_size);
    initialize_free_list(old_size, new_size);
    free_head = static_cast<PoolIndex>(old_size);
  }

  // Acquires a free spot in the order pool
  PoolIndex acquire_and_place(const Order &order) {
    // Expand our order pool if needed
    if (free_head == kInvalidIndex) {
      grow();
    }

    // Get and return the next available index
    PoolIndex next_free_index = free_head;
    free_head = orders[next_free_index].next; // Increment free head

    // Place the order into the pool
    orders[next_free_index] = order;
    orders[next_free_index].prev = kInvalidIndex;
    orders[next_free_index].next = kInvalidIndex;

    return next_free_index;
  }

  // Release a spot in the order pool
  void release(PoolIndex idx) {
    assert(idx < orders.size());
    orders[idx].next = free_head;
    orders[idx].prev = kInvalidIndex;
    free_head = idx;
  }

  void initialize_free_list(std::size_t begin, std::size_t end) {
    // Set the next index on each order
    for (std::size_t i = begin; i + 1 < end; i++) {
      orders[i].next = static_cast<PoolIndex>(i + 1);
    }

    // Set the last order's next ptr to be invalid
    if (end > begin) {
      orders[end - 1].next = kInvalidIndex;
    }
  }

  std::vector<Order> orders;
  PoolIndex free_head = kInvalidIndex;
};

#endif