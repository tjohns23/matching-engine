#include "matching-engine.h"
#include "order-book.h"
#include "order.h"
#include <cassert>
#include <gtest/gtest.h>
#include <iostream>

// -----------------------------------------------------------------------
// Limit order: Basic Resting
// -----------------------------------------------------------------------

// A limit order submitted with no opposing liquidity should rest in the book.
TEST(LimitOrder, RestsWhenNoLiquidity) {
  OrderBook book;
  Order order{1, Side::Buy, 100, 50, Type::Limit, 1};
  book.submit_order(order);

  EXPECT_TRUE(book.has_order(1));
  EXPECT_TRUE(book.has_price_level(Side::Buy, 100));
  EXPECT_EQ(book.bid_count(), 1u);
  EXPECT_EQ(book.remaining_qty_of(1), 50);
  EXPECT_EQ(book.best_bid(), 100);
}

// -----------------------------------------------------------------------
// Market order
// -----------------------------------------------------------------------

// A market order with no opposing liquidity should NOT rest in the book.
TEST(MarketOrder, DoesNotRestWhenNoLiquidity) {
  OrderBook book;
  Order order{1, Side::Buy, 0, 50, Type::Market, 1};
  book.submit_order(order);

  EXPECT_FALSE(book.has_order(1));
  EXPECT_EQ(book.bid_count(), 0u);
  EXPECT_EQ(order.remaining_qty, 50); // unfilled, but discarded — not in book
}

// A market buy should fully fill against a resting limit sell.
TEST(MarketOrder, BuyFillsAgainstRestingLimitSell) {
  OrderBook book;

  Order sell{1, Side::Sell, 100, 50, Type::Limit, 1};
  book.submit_order(sell);
  ASSERT_TRUE(book.has_order(1));

  Order buy{2, Side::Buy, 0, 50, Type::Market, 2};
  book.submit_order(buy);

  // Market buy fully filled — does not rest
  EXPECT_EQ(buy.remaining_qty, 0);
  EXPECT_FALSE(book.has_order(2));

  // Resting sell fully consumed
  EXPECT_FALSE(book.has_order(1));
  EXPECT_EQ(book.ask_count(), 0u);
}

// A market sell should fully fill against a resting limit buy.
TEST(MarketOrder, SellFillsAgainstRestingLimitBuy) {
  OrderBook book;

  Order buy{1, Side::Buy, 100, 50, Type::Limit, 1};
  book.submit_order(buy);
  ASSERT_TRUE(book.has_order(1));

  Order sell{2, Side::Sell, 0, 50, Type::Market, 2};
  book.submit_order(sell);

  // Market sell fully filled — does not rest
  EXPECT_EQ(sell.remaining_qty, 0);
  EXPECT_FALSE(book.has_order(2));

  // Resting buy fully consumed
  EXPECT_FALSE(book.has_order(1));
  EXPECT_EQ(book.bid_count(), 0u);
}

// -----------------------------------------------------------------------
// Limit order: immediate fill (no resting)
// -----------------------------------------------------------------------

// A limit buy whose price >= best ask should fill immediately and NOT rest.
TEST(LimitOrder, FillsImmediatelyAndDoesNotRest) {
  OrderBook book;

  Order sell{1, Side::Sell, 100, 50, Type::Limit, 1};
  book.submit_order(sell);

  Order buy{2, Side::Buy, 100, 50, Type::Limit, 2};
  book.submit_order(buy);

  // Limit buy was fully filled — should not rest
  EXPECT_FALSE(book.has_order(2));
  EXPECT_EQ(buy.remaining_qty, 0);

  // Resting sell consumed
  EXPECT_FALSE(book.has_order(1));
  EXPECT_EQ(book.ask_count(), 0u);
  EXPECT_EQ(book.bid_count(), 0u);
}

// A limit buy whose price < best ask should NOT fill and should rest.
TEST(LimitOrder, DoesNotFillWhenPriceTooLow) {
  OrderBook book;

  Order sell{1, Side::Sell, 105, 50, Type::Limit, 1};
  book.submit_order(sell);

  Order buy{2, Side::Buy, 100, 50, Type::Limit, 2}; // price doesn't cross ask
  book.submit_order(buy);

  // Buy should rest unfilled
  EXPECT_TRUE(book.has_order(2));
  EXPECT_EQ(book.remaining_qty_of(2), 50);
  EXPECT_EQ(book.bid_count(), 1u);

  // Resting sell untouched
  EXPECT_TRUE(book.has_order(1));
  EXPECT_EQ(book.ask_count(), 1u);
}

// -----------------------------------------------------------------------
// Limit order: partial fill then rest
// -----------------------------------------------------------------------

// A limit buy for more qty than available should partially fill,
// then rest in the book with the unfilled remainder.
TEST(LimitOrder, PartiallyFillsThenRests) {
  OrderBook book;

  // Only 30 units available at ask 100
  Order sell{1, Side::Sell, 100, 30, Type::Limit, 1};
  book.submit_order(sell);

  // Buy 50 at 100 — fills 30, rests with qty 20
  Order buy{2, Side::Buy, 100, 50, Type::Limit, 2};
  book.submit_order(buy);

  // Resting sell fully consumed
  EXPECT_FALSE(book.has_order(1));
  EXPECT_EQ(book.ask_count(), 0u);

  // Limit buy rests with the unfilled remainder
  EXPECT_TRUE(book.has_order(2));
  EXPECT_EQ(book.remaining_qty_of(2), 20);
  EXPECT_EQ(book.level_qty(Side::Buy, 100), 20);
  EXPECT_EQ(book.bid_count(), 1u);
}

// -----------------------------------------------------------------------
// Resting limit order is removed when fully matched
// -----------------------------------------------------------------------

// A resting limit sell should be removed from the book once fully matched.
TEST(LimitOrder, RestingOrderRemovedWhenFilled) {
  OrderBook book;

  Order sell{1, Side::Sell, 100, 50, Type::Limit, 1};
  book.submit_order(sell);
  ASSERT_TRUE(book.has_order(1));
  ASSERT_TRUE(book.has_price_level(Side::Sell, 100));

  // Incoming limit buy fully matches the resting sell
  Order buy{2, Side::Buy, 100, 50, Type::Limit, 2};
  book.submit_order(buy);

  EXPECT_FALSE(book.has_order(1));
  EXPECT_FALSE(book.has_price_level(Side::Sell, 100));
  EXPECT_EQ(book.ask_count(), 0u);
}