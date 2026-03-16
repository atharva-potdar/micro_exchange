#include "memory/arena.hpp"
#include "orderbook/types.hpp"
#include <gtest/gtest.h>

TEST(Arena, NonNullAlloc) {
  Arena<Order, 10> order_pool;

  Order *order1 = order_pool.allocate();

  ASSERT_NE(order1, nullptr) << "Arena failed to allocate the first order.";
}

TEST(Arena, DeallocAlloc) {
  Arena<Order, 10> order_pool;

  Order *order1 = order_pool.allocate();

  order_pool.deallocate(order1);

  Order *order2 = order_pool.allocate();

  ASSERT_EQ(order1, order2)
      << "Arena doesn't return same address on allocation after deallocation.";
}

TEST(Arena, CapacityAllocs) {
  Arena<Order, 10> order_pool;

  int i = 0;
  while (order_pool.allocate() != nullptr) {
    i++;
  }

  ASSERT_EQ(i, 10) << "Arena does not obey the CAPACITY template parameter.";
}

TEST(Arena, ExhaustAndRecover) {
  Arena<Order, 10> order_pool;
  Order *orders[10];
  for (int i = 0; i < 10; i++) {
    orders[i] = order_pool.allocate();
  }

  ASSERT_EQ(order_pool.allocate(), nullptr);

  order_pool.deallocate(orders[7]);

  Order *recovered_order = order_pool.allocate();

  ASSERT_EQ(recovered_order, orders[7])
      << "Arena failed to recover correct slot after exhaustion.";
  ASSERT_EQ(order_pool.allocate(), nullptr);
}

TEST(Arena, NullDeallocation) {
  Arena<Order, 64> arena;
  arena.deallocate(nullptr); // should not crash
  ASSERT_EQ(arena.size(), 0);
}

TEST(Arena, AlignmentValidation) {
  Arena<Order, 64> arena;
  Order *o = arena.allocate(1ULL, 100ULL, 10U, Side::Buy, nullptr, nullptr);
  ASSERT_EQ(reinterpret_cast<uintptr_t>(o) % alignof(Order), 0);
}
