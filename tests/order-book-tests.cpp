#include "matching-engine.h"
#include "order-book.h"
#include "order.h"
#include <cassert>
#include <gtest/gtest.h>
#include <iostream>
#include <stdexcept>

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
  EXPECT_TRUE(book.trades().empty());
}

// A resting buy should be fully removed once matched
TEST(LimitOrder, RestingBuyRemovedWhenFilled) {
  OrderBook book;

  Order buy{1, Side::Buy, 100, 50, Type::Limit, 1};
  book.submit_order(buy);
  ASSERT_TRUE(book.has_order(1));
  ASSERT_TRUE(book.has_price_level(Side::Buy, 100));

  Order sell{2, Side::Sell, 100, 50, Type::Limit, 2};
  book.submit_order(sell);

  EXPECT_FALSE(book.has_order(1));
  EXPECT_FALSE(book.has_price_level(Side::Buy, 100));
  EXPECT_EQ(book.bid_count(), 0u);
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
  EXPECT_TRUE(book.trades().empty());
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

  // Trade log reflects the fill correctly
  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1);
  EXPECT_EQ(book.trades()[0].taker_order_id, 2);
  EXPECT_EQ(book.trades()[0].price, 100);
  EXPECT_EQ(book.trades()[0].qty, 50);
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

  // Trade log reflects the fill correctly
  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1);
  EXPECT_EQ(book.trades()[0].taker_order_id, 2);
  EXPECT_EQ(book.trades()[0].price, 100);
  EXPECT_EQ(book.trades()[0].qty, 50);
}

// -----------------------------------------------------------------------
// Limit order: immediate fill (no resting)
// -----------------------------------------------------------------------

// A limit buy should fill immediately and NOT rest when theres enough
// liquidity.
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

  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1);
  EXPECT_EQ(book.trades()[0].taker_order_id, 2);
  EXPECT_EQ(book.trades()[0].qty, 50);
}

// A limit buy priced ABOVE the resting ask should still trade at the
// resting (maker's) price, never at the aggressor's limit price.
TEST(LimitOrder, TradesAtMakerPriceNotTakerPrice) {
  OrderBook book;

  Order sell{1, Side::Sell, 100, 50, Type::Limit, 1}; // resting ask at 100
  book.submit_order(sell);

  Order buy{2, Side::Buy, 105, 50, Type::Limit, 2}; // willing to pay up to 105
  book.submit_order(buy);

  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].price, 100); // must fill at maker's price, not 105
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

  EXPECT_TRUE(book.trades().empty());
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

  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1);
  EXPECT_EQ(book.trades()[0].taker_order_id, 2);
  EXPECT_EQ(book.trades()[0].qty, 30); // only the filled portion, not 50
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

// -----------------------------------------------------------------------
// Price-time priority (FIFO within a price level)
// -----------------------------------------------------------------------

// Two resting sells at the same price — the OLDER one must fill first.
TEST(PriceTimePriority, SellSideFillsOldestFirst) {
  OrderBook book;
  Order first{1, Side::Sell, 100, 30, Type::Limit, 1};
  Order second{2, Side::Sell, 100, 30, Type::Limit, 2};
  book.submit_order(first);
  book.submit_order(second);

  Order buy{3, Side::Buy, 100, 30, Type::Limit, 3};
  book.submit_order(buy);

  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1); // oldest, not 2

  EXPECT_FALSE(book.has_order(1));
  EXPECT_TRUE(book.has_order(2));
  EXPECT_EQ(book.remaining_qty_of(2), 30);
}

// Mirror on the buy side — resting bids at the same price fill oldest first.
TEST(PriceTimePriority, BuySideFillsOldestFirst) {
  OrderBook book;
  Order first{1, Side::Buy, 100, 30, Type::Limit, 1};
  Order second{2, Side::Buy, 100, 30, Type::Limit, 2};
  book.submit_order(first);
  book.submit_order(second);

  Order sell{3, Side::Sell, 100, 30, Type::Limit, 3};
  book.submit_order(sell);

  ASSERT_EQ(book.trades().size(), 1u);
  EXPECT_EQ(book.trades()[0].maker_order_id, 1); // oldest, not 2

  EXPECT_FALSE(book.has_order(1));
  EXPECT_TRUE(book.has_order(2));
  EXPECT_EQ(book.remaining_qty_of(2), 30);
}

// -----------------------------------------------------------------------
// Walking multiple price levels
// -----------------------------------------------------------------------

// An aggressive limit buy that exceeds the best level's quantity should
// walk into the next best price level and fill there too.
TEST(LimitOrder, WalksMultiplePriceLevels) {
  OrderBook book;
  Order sell1{1, Side::Sell, 100, 10, Type::Limit, 1};
  book.submit_order(sell1);
  Order sell2{2, Side::Sell, 101, 10, Type::Limit, 2};
  book.submit_order(sell2);

  Order buy{3, Side::Buy, 101, 20, Type::Limit, 3};
  book.submit_order(buy);

  EXPECT_EQ(buy.remaining_qty, 0);
  EXPECT_FALSE(book.has_order(1));
  EXPECT_FALSE(book.has_order(2));

  ASSERT_EQ(book.trades().size(), 2u);
  EXPECT_EQ(book.trades()[0].price, 100); // best price fills first
  EXPECT_EQ(book.trades()[0].qty, 10);
  EXPECT_EQ(book.trades()[1].price, 101);
  EXPECT_EQ(book.trades()[1].qty, 10);
}

// Mirror on the sell side — a market sell should walk down through
// multiple bid levels, best (highest) bid first.
TEST(MarketOrder, SellWalksMultipleBidLevels) {
  OrderBook book;
  Order buy1{1, Side::Buy, 100, 10, Type::Limit, 1};
  book.submit_order(buy1);
  Order buy2{2, Side::Buy, 99, 10, Type::Limit, 2};
  book.submit_order(buy2);

  Order sell{3, Side::Sell, 0, 20, Type::Market, 3};
  book.submit_order(sell);

  EXPECT_EQ(sell.remaining_qty, 0);
  EXPECT_FALSE(book.has_order(1));
  EXPECT_FALSE(book.has_order(2));

  ASSERT_EQ(book.trades().size(), 2u);
  EXPECT_EQ(book.trades()[0].price, 100); // best (highest) bid fills first
  EXPECT_EQ(book.trades()[1].price, 99);
}

// -----------------------------------------------------------------------
// cancel_order
// -----------------------------------------------------------------------

TEST(CancelOrder, RemovesRestingOrder) {
  OrderBook book;
  Order order{1, Side::Buy, 100, 50, Type::Limit, 1};
  book.submit_order(order);

  book.cancel_order(1);

  EXPECT_FALSE(book.has_order(1));
  EXPECT_FALSE(book.has_price_level(Side::Buy, 100));
  EXPECT_EQ(book.bid_count(), 0u);
}

// -----------------------------------------------------------------------
// Accessor exception behavior on missing orders
// -----------------------------------------------------------------------

TEST(Accessors, RemainingQtyOfMissingOrderThrows) {
  OrderBook book;
  EXPECT_THROW(book.remaining_qty_of(999), std::out_of_range);
}

TEST(Accessors, SideOfMissingOrderThrows) {
  OrderBook book;
  EXPECT_THROW(book.side_of(999), std::out_of_range);
}

TEST(Accessors, PriceOfMissingOrderThrows) {
  OrderBook book;
  EXPECT_THROW(book.price_of(999), std::out_of_range);
}

TEST(Accessors, FrontOrderIdOnMissingLevelThrows) {
  OrderBook book;
  EXPECT_THROW(book.front_order_id(Side::Buy, 100), std::out_of_range);
}

// -----------------------------------------------------------------------
// Empty book baseline
// -----------------------------------------------------------------------

TEST(EmptyBook, HasNoBestPricesOrLevels) {
  OrderBook book;

  EXPECT_FALSE(book.best_bid().has_value());
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.bid_count(), 0u);
  EXPECT_EQ(book.ask_count(), 0u);
  EXPECT_TRUE(book.trades().empty());
}