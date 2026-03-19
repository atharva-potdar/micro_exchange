#include <gtest/gtest.h>

#include "memory/arena.hpp"
#include "orderbook/types.hpp"

TEST(PriceLevel, PushThreeOrdersVerifyFront) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  ASSERT_EQ(p.front(), a);
  ASSERT_EQ(p.front()->id, 1);
  ASSERT_EQ(p.order_count, 3);
  ASSERT_EQ(p.total_quantity, 60);
}

TEST(PriceLevel, RemoveMiddle) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  p.remove(b);

  ASSERT_EQ(p.front(), a);
  ASSERT_EQ(a->next, c);
  ASSERT_EQ(c->prev, a);
  ASSERT_EQ(p.order_count, 2);
}

TEST(PriceLevel, RemoveHead) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  p.remove(a);

  ASSERT_EQ(p.front(), b);
  ASSERT_EQ(b->prev, nullptr);
  ASSERT_EQ(p.order_count, 2);
}

TEST(PriceLevel, RemoveTail) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  p.remove(c);

  ASSERT_EQ(p.tail, b);
  ASSERT_EQ(b->next, nullptr);
  ASSERT_EQ(p.order_count, 2);
}

TEST(PriceLevel, RemoveLastOrder) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.remove(a);

  ASSERT_EQ(p.empty(), true);
  ASSERT_EQ(p.head, nullptr);
  ASSERT_EQ(p.tail, nullptr);
  ASSERT_EQ(p.order_count, 0);
  ASSERT_EQ(p.total_quantity, 0);
}

TEST(PriceLevel, QuantityTracking) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  ASSERT_EQ(p.total_quantity, 60);

  p.remove(b);

  ASSERT_EQ(p.total_quantity, 40);
}

TEST(PriceLevel, ZeroQuantityGuard) {
  Arena<Order, 64> order_pool;
  PriceLevel level;
  level.price = 100;

  Order* o = order_pool.allocate(1ULL, 100ULL, 0U, Side::Buy, nullptr, nullptr);

  ASSERT_DEATH(level.push_back(o), "Cannot push an order with 0 quantity")
      << "Should assert on zero quantity.";
}

TEST(PriceLevel, PriceMismatchGuard) {
  Arena<Order, 64> order_pool;
  PriceLevel level;
  level.price = 100;

  Order* o =
      order_pool.allocate(1ULL, 999ULL, 10U, Side::Buy, nullptr, nullptr);

  ASSERT_DEATH(level.push_back(o),
               "Order price does not match PriceLevel price")
      << "Should assert on price mismatch.";
}

TEST(PriceLevel, FIFOIntegrity) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  ASSERT_EQ(p.head, a) << "Head should be first inserted order.";
  ASSERT_EQ(a->next, b) << "First order's next should be second order.";
  ASSERT_EQ(b->next, c) << "Second order's next should be third order.";
  ASSERT_EQ(c->next, nullptr) << "Last order's next should be nullptr.";

  ASSERT_EQ(p.tail, c) << "Tail should be last inserted order.";
  ASSERT_EQ(c->prev, b) << "Last order's prev should be second order.";
  ASSERT_EQ(b->prev, a) << "Second order's prev should be first order.";
  ASSERT_EQ(a->prev, nullptr) << "First order's prev should be nullptr.";
}

TEST(PriceLevel, TailVerification) {
  Arena<Order, 64> arena;
  PriceLevel p;
  p.price = 100;

  Order* a = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  Order* b = arena.allocate(2ULL, 100ULL, 20U, Side::Buy, nullptr, nullptr);
  Order* c = arena.allocate(3ULL, 100ULL, 30U, Side::Buy, nullptr, nullptr);

  p.push_back(a);
  p.push_back(b);
  p.push_back(c);

  ASSERT_EQ(p.tail, c) << "Tail should be third order.";
  p.remove(c);
  ASSERT_EQ(p.tail, b) << "Tail should be second order after removing third.";
  p.remove(b);
  ASSERT_EQ(p.tail, a) << "Tail should be first order after removing second.";
  p.remove(a);
  ASSERT_EQ(p.tail, nullptr) << "Tail should be nullptr after removing all.";
}
