#include "price-level-array.h"

#include <gtest/gtest.h>
#include <stdexcept>

TEST(PriceLevelArray, ConstructorCreatesFixedPriceBand) {
  priceLevelArray levels(5929, 12450);

  EXPECT_EQ(levels.get_min_price(), 5929);
  EXPECT_EQ(levels.get_max_price(), 12450);
  EXPECT_EQ(levels.size(), 6522u);

  EXPECT_EQ(levels.get_level(5929).price, 5929);
  EXPECT_EQ(levels.get_level(12450).price, 12450);
}

TEST(PriceLevelArray, GetLevelRejectsPricesOutsideBand) {
  priceLevelArray levels(100, 105);

  EXPECT_THROW(levels.get_level(99), std::out_of_range);
  EXPECT_THROW(levels.get_level(106), std::out_of_range);
  EXPECT_NO_THROW(levels.get_level(100));
  EXPECT_NO_THROW(levels.get_level(105));
}

TEST(PriceLevelArray, HasLevelTracksBitmapActivation) {
  priceLevelArray levels(100, 105);

  EXPECT_FALSE(levels.has_level(101));

  PriceLevel &level = levels.get_level(101);
  level.order_count = 2;
  levels.on_level_updated(101);

  EXPECT_TRUE(levels.has_level(101));

  level.order_count = 0;
  levels.on_level_updated(101);

  EXPECT_FALSE(levels.has_level(101));
}

TEST(PriceLevelArray, BestBidAndBestAskReturnActivePriceLevels) {
  priceLevelArray levels(100, 110);

  for (Price price : {100, 103, 107, 110}) {
    levels.get_level(price).order_count = 1;
    levels.on_level_updated(price);
  }

  ASSERT_TRUE(levels.best_bid().has_value());
  ASSERT_TRUE(levels.best_ask().has_value());

  EXPECT_EQ(levels.best_bid().value(), 110);
  EXPECT_EQ(levels.best_ask().value(), 100);
}

TEST(PriceLevelArray, BestPricesMoveWhenEdgeLevelsBecomeInactive) {
  priceLevelArray levels(100, 110);

  for (Price price : {100, 103, 107, 110}) {
    levels.get_level(price).order_count = 1;
    levels.on_level_updated(price);
  }

  levels.get_level(110).order_count = 0;
  levels.on_level_updated(110);
  EXPECT_EQ(levels.best_bid().value(), 107);

  levels.get_level(100).order_count = 0;
  levels.on_level_updated(100);
  EXPECT_EQ(levels.best_ask().value(), 103);

  for (Price price : {103, 107}) {
    levels.get_level(price).order_count = 0;
    levels.on_level_updated(price);
  }

  EXPECT_FALSE(levels.best_bid().has_value());
  EXPECT_FALSE(levels.best_ask().has_value());
}

TEST(PriceLevelArray, OutOfRangePricesAreIgnoredByOnLevelUpdated) {
  priceLevelArray levels(100, 105);

  EXPECT_NO_THROW(levels.on_level_updated(99));
  EXPECT_NO_THROW(levels.on_level_updated(106));
  EXPECT_FALSE(levels.has_level(100));
}
