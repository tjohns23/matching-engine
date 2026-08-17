#include "order-pool.h"
#include <gtest/gtest.h>

namespace {

Order make_order(OrderId id, Qty qty = 10) {
  return Order{id, Side::Buy, 100, qty, Type::Limit, 1};
}

} // namespace

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------

// The free list should be threaded 0 -> 1 -> ... -> capacity-1 -> INVALID
// at construction, with free_head starting at 0. We can't read free_head
// directly (it's private), so we verify it indirectly: the very first
// acquire should hand out slot 0.
TEST(OrderPool, ConstructorInitializesFreeListAndCapacity) {
  OrderPool pool(4);
  EXPECT_EQ(pool.capacity(), 4u);

  PoolIndex idx = pool.add_pool_order(make_order(1));
  EXPECT_EQ(idx, 0u);
}

TEST(OrderPool, DefaultCapacityIsOneThousandTwentyFour) {
  OrderPool pool;
  EXPECT_EQ(pool.capacity(), 1024u);
}

TEST(OrderPool, ZeroInitialCapacityGrowsOnFirstAdd) {
  OrderPool pool(0);
  EXPECT_EQ(pool.capacity(), 0u);

  PoolIndex idx = pool.add_pool_order(make_order(1));
  EXPECT_EQ(idx, 0u);
  EXPECT_EQ(pool.capacity(), 1024u);
}

// -----------------------------------------------------------------------
// Adding
// -----------------------------------------------------------------------

TEST(OrderPool, AddOrderStoresTheOrder) {
  OrderPool pool(4);
  Order order{7, Side::Sell, 250, 30, Type::Limit, 9};

  PoolIndex idx = pool.add_pool_order(order);

  EXPECT_EQ(pool[idx].id, 7u);
  EXPECT_EQ(pool[idx].side, Side::Sell);
  EXPECT_EQ(pool[idx].price, 250);
  EXPECT_EQ(pool[idx].remaining_qty, 30);
  EXPECT_EQ(pool[idx].type, Type::Limit);
  EXPECT_EQ(pool[idx].sequence, 9u);
}

// acquire() must overwrite prev/next on the stored copy rather than
// trusting whatever the caller's Order happened to carry in.
TEST(OrderPool, AddOrderResetsLinksOnTheStoredCopy) {
  OrderPool pool(4);
  Order order{1, Side::Buy, 100, 10, Type::Limit, 1};
  order.prev = 99;
  order.next = 99;

  PoolIndex idx = pool.add_pool_order(order);

  EXPECT_EQ(pool[idx].prev, kInvalidIndex);
  EXPECT_EQ(pool[idx].next, kInvalidIndex);
}

// Double check this
TEST(OrderPool, AddOrderAdvancesFreeHead) {
  OrderPool pool(4);
  PoolIndex idx0 = pool.add_pool_order(make_order(1));
  PoolIndex idx1 = pool.add_pool_order(make_order(2));

  EXPECT_EQ(idx0, 0u);
  EXPECT_EQ(idx1, 1u);
}

TEST(OrderPool, AddMultipleOrdersStoresEachCorrectly) {
  OrderPool pool(4);
  Order a{1, Side::Buy, 100, 10, Type::Limit, 1};
  Order b{2, Side::Sell, 101, 20, Type::Limit, 2};
  Order c{3, Side::Buy, 99, 30, Type::Market, 3};

  PoolIndex ia = pool.add_pool_order(a);
  PoolIndex ib = pool.add_pool_order(b);
  PoolIndex ic = pool.add_pool_order(c);

  EXPECT_NE(ia, ib);
  EXPECT_NE(ib, ic);
  EXPECT_NE(ia, ic);

  EXPECT_EQ(pool[ia].id, 1u);
  EXPECT_EQ(pool[ib].id, 2u);
  EXPECT_EQ(pool[ic].id, 3u);
}

// With the free list threaded 0 -> 1 -> 2 -> 3 at construction, four
// consecutive adds should consume slots in exactly that order with no
// growth in between.
// Double check this
TEST(OrderPool, AddMultipleOrdersAdvancesFreeHeadSequentially) {
  OrderPool pool(4);

  EXPECT_EQ(pool.add_pool_order(make_order(1)), 0u);
  EXPECT_EQ(pool.add_pool_order(make_order(2)), 1u);
  EXPECT_EQ(pool.add_pool_order(make_order(3)), 2u);
  EXPECT_EQ(pool.add_pool_order(make_order(4)), 3u);
  EXPECT_EQ(pool.capacity(), 4u);
}

// -----------------------------------------------------------------------
// Growing
// -----------------------------------------------------------------------

TEST(OrderPool, DoesNotGrowUntilCapacityExhausted) {
  OrderPool pool(2);
  pool.add_pool_order(make_order(1));
  EXPECT_EQ(pool.capacity(), 2u);
  pool.add_pool_order(make_order(2));
  EXPECT_EQ(pool.capacity(), 2u);
}

TEST(OrderPool, GrowsWhenCapacityExhausted) {
  OrderPool pool(2);
  pool.add_pool_order(make_order(1));
  pool.add_pool_order(make_order(2));

  PoolIndex idx = pool.add_pool_order(make_order(3));

  EXPECT_EQ(pool.capacity(), 4u); // doubled
  EXPECT_EQ(idx, 2u);             // first slot in the new range
}

// Regression test: after growing, every new slot must be individually
// usable without triggering another grow. If the new range isn't threaded
// into the free list, only the first new slot is reachable and every
// subsequent add re-triggers grow() -- capacity balloons for nothing.
TEST(OrderPool, AllNewSlotsAfterGrowAreUsableWithoutRegrowing) {
  OrderPool pool(2);
  pool.add_pool_order(make_order(1));
  pool.add_pool_order(make_order(2)); // pool full, capacity 2
  pool.add_pool_order(make_order(3)); // triggers grow -> capacity 4
  ASSERT_EQ(pool.capacity(), 4u);

  PoolIndex idx = pool.add_pool_order(make_order(4));
  EXPECT_EQ(idx, 3u);
  EXPECT_EQ(pool.capacity(), 4u); // must NOT have grown again
}

// -----------------------------------------------------------------------
// Releasing
// -----------------------------------------------------------------------

TEST(OrderPool, ReleaseMakesSlotReusable) {
  OrderPool pool(4);
  PoolIndex idx0 = pool.add_pool_order(make_order(1));
  pool.add_pool_order(make_order(2));

  pool.remove_pool_order(idx0);
  PoolIndex idx_reused = pool.add_pool_order(make_order(3));

  EXPECT_EQ(idx_reused, idx0);
  EXPECT_EQ(pool[idx_reused].id, 3u);
  EXPECT_EQ(pool.capacity(), 4u); // reused the slot, no grow needed
}

TEST(OrderPool, ReleaseUpdatesFreeHeadToReleasedSlot) {
  OrderPool pool(4);
  pool.add_pool_order(make_order(1));                  // idx 0
  PoolIndex idx1 = pool.add_pool_order(make_order(2)); // idx 1
  pool.add_pool_order(make_order(3));                  // idx 2

  pool.remove_pool_order(idx1);

  // free_head should now point at idx1 -- the very next add reuses it
  // instead of continuing on to whatever slot was next before the release.
  PoolIndex idx = pool.add_pool_order(make_order(4));
  EXPECT_EQ(idx, idx1);
}

TEST(OrderPool, ReleaseMultipleUpdatesFreeHeadInLifoOrder) {
  OrderPool pool(4);
  pool.add_pool_order(make_order(1));                  // idx 0
  PoolIndex idx1 = pool.add_pool_order(make_order(2)); // idx 1
  pool.add_pool_order(make_order(3));                  // idx 2
  PoolIndex idx3 = pool.add_pool_order(make_order(4)); // idx 3
  ASSERT_EQ(pool.capacity(), 4u);                      // pool now full

  pool.remove_pool_order(idx1); // release slot 1 first
  pool.remove_pool_order(idx3); // release slot 3 second

  // The free list is LIFO: the most recently released slot (idx3) comes
  // back out first, then idx1.
  PoolIndex first_reused = pool.add_pool_order(make_order(5));
  PoolIndex second_reused = pool.add_pool_order(make_order(6));

  EXPECT_EQ(first_reused, idx3);
  EXPECT_EQ(second_reused, idx1);
  EXPECT_EQ(pool.capacity(), 4u); // both reused, no grow triggered
}

TEST(OrderPool, ReleaseResetsPrevOnTheFreedSlot) {
  OrderPool pool(4);
  PoolIndex idx0 = pool.add_pool_order(make_order(1));
  pool.add_pool_order(make_order(2));

  pool.remove_pool_order(idx0);

  EXPECT_EQ(pool[idx0].prev, kInvalidIndex);
}