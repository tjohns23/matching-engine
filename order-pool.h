#ifndef ORDER_POOL_H
#define ORDER_POOL_H

#include "order.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <vector>

/**************************************************************
 * The order pool is a pre-allocated block of memory to hold incoming orders.
 * The OrderPool class manages each 'slot' of memory.
 * Note: The order pool does not set the prev or next pointers
 *       of the order nodes it manages. It only gives them a slot of memory
 *       in the pool.
 **************************************************************/
class OrderPool {
public:
  // Constructor
  explicit OrderPool(std::size_t initial_capacity = 1024) {
    orders.resize(initial_capacity);
    initialize_free_list(0, initial_capacity);
    free_head = initial_capacity > 0 ? 0 : kInvalidIndex;
  }

  // Add an order to the pool
  PoolIndex add_pool_order(const Order &order) {
    return acquire_and_place(order);
  }

  // Remove an order from the pool
  void remove_pool_order(PoolIndex index) { release(index); }

  // Override the array index operator
  Order &operator[](PoolIndex index) {
    assert(index < orders.size());
    return orders[index];
  }

  const Order &operator[](PoolIndex index) const {
    assert(index < orders.size());
    return orders[index];
  }

  // Get the capacity of the pool
  std::size_t capacity() const { return orders.size(); }

private:
  // Resizes the order pool when its full
  void grow() {
    std::size_t old_size = orders.size();
    std::size_t new_size = old_size == 0 ? 1024 : old_size * 2;
    orders.resize(new_size);
    initialize_free_list(old_size, new_size);
    free_head = static_cast<PoolIndex>(old_size);
  }

  // Initializes the free list for a range of newly allocated slots [begin,
  // end).
  //
  // WHEN IT IS USED: This is called during the pool's initial construction and
  // whenever the underlying array capacity needs to grow.
  //
  // WHY WE DO IT: It pre-links the newly created, unallocated slots together
  // into a singly-linked list. This maintains the essential invariant that
  // allows us to accurately track the next free slot in the pool, ensuring
  // we can always acquire available memory in O(1) time without searching.
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

  // Acquires a free spot in the order pool and places the order there
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
  void release(PoolIndex index) {
    assert(index < orders.size());
    orders[index].next = free_head;
    orders[index].prev = kInvalidIndex;
    free_head = index;
  }

  std::vector<Order> orders;
  PoolIndex free_head = kInvalidIndex;
};

#endif