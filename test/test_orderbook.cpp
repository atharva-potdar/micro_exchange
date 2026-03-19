#include <gtest/gtest.h>

#include <orderbook/orderbook.hpp>

TEST(OrderBook, BuyOrdersSortDescending) {
  OrderBook<1024, 1024> b;
  ASSERT_TRUE(b.add_order<Side::Buy>(1, 100, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(2, 105, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(3, 95, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(4, 110, 10).has_value());

  EXPECT_EQ(b.get_level_count<Side::Buy>(), 4);
  EXPECT_EQ(b.get_active_level_count(), 4);
  EXPECT_EQ(b.get_active_order_count(), 4);

  EXPECT_EQ(b.get_level<Side::Buy>(0)->price, 110);
  EXPECT_EQ(b.get_level<Side::Buy>(1)->price, 105);
  EXPECT_EQ(b.get_level<Side::Buy>(2)->price, 100);
  EXPECT_EQ(b.get_level<Side::Buy>(3)->price, 95);
}

TEST(OrderBook, SellOrdersSortAscending) {
  OrderBook<1024, 1024> b;
  ASSERT_TRUE(b.add_order<Side::Sell>(1, 100, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Sell>(2, 105, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Sell>(3, 95, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Sell>(4, 110, 10).has_value());

  EXPECT_EQ(b.get_level_count<Side::Sell>(), 4);
  EXPECT_EQ(b.get_active_level_count(), 4);
  EXPECT_EQ(b.get_active_order_count(), 4);

  EXPECT_EQ(b.get_level<Side::Sell>(0)->price, 95);
  EXPECT_EQ(b.get_level<Side::Sell>(1)->price, 100);
  EXPECT_EQ(b.get_level<Side::Sell>(2)->price, 105);
  EXPECT_EQ(b.get_level<Side::Sell>(3)->price, 110);
}

TEST(OrderBook, OrderAggregationAtSamePrice) {
  OrderBook<1024, 1024> b;
  ASSERT_TRUE(b.add_order<Side::Buy>(1, 100, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(2, 100, 20).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(3, 100, 30).has_value());

  EXPECT_EQ(b.get_level_count<Side::Buy>(), 1);
  EXPECT_EQ(b.get_active_level_count(), 1);
  EXPECT_EQ(b.get_active_order_count(), 3);

  const PriceLevel* level = b.get_level<Side::Buy>(0);
  EXPECT_EQ(level->order_count, 3);
  EXPECT_EQ(level->total_quantity, 60);
  EXPECT_EQ(level->head->id, 1);
  EXPECT_EQ(level->tail->id, 3);
}

TEST(OrderBook, CancelMiddleOrder) {
  OrderBook<1024, 1024> b;
  ASSERT_TRUE(b.add_order<Side::Buy>(1, 100, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(2, 100, 20).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(3, 100, 30).has_value());

  EXPECT_TRUE(b.cancel_order(2).has_value());
  EXPECT_EQ(b.get_order(2), nullptr);

  const PriceLevel* level = b.get_level<Side::Buy>(0);
  EXPECT_EQ(level->order_count, 2);
  EXPECT_EQ(level->total_quantity, 40);
  EXPECT_EQ(level->head->next->id, 3);
  EXPECT_EQ(level->tail->prev->id, 1);
  EXPECT_EQ(b.get_active_order_count(), 2);
  EXPECT_EQ(b.get_active_level_count(), 1);
}

TEST(OrderBook, CancelLastOrderDeallocatesLevel) {
  OrderBook<1024, 1024> b;
  ASSERT_TRUE(b.add_order<Side::Buy>(1, 100, 10).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(2, 99, 20).has_value());
  ASSERT_TRUE(b.add_order<Side::Buy>(3, 98, 30).has_value());

  EXPECT_EQ(b.get_level_count<Side::Buy>(), 3);
  EXPECT_EQ(b.get_active_level_count(), 3);

  EXPECT_TRUE(b.cancel_order(2).has_value());

  EXPECT_EQ(b.get_level_count<Side::Buy>(), 2);
  EXPECT_EQ(b.get_level<Side::Buy>(0)->price, 100);
  EXPECT_EQ(b.get_level<Side::Buy>(1)->price, 98);

  EXPECT_EQ(b.get_active_level_count(), 2);
  EXPECT_EQ(b.get_active_order_count(), 2);
}
