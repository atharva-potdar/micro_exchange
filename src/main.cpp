#include <iostream>
#include <memory>

#include "orderbook/orderbook.hpp"

// To use as a target for profiling
int main() {
  std::cout << "Hello, matching engine!\n";

  auto book = std::make_unique<OrderBook<1'000'000, 100'000>>();
  constexpr uint64_t NUM_OPERATIONS = 200'000'000;

  for (uint64_t i = 1; i <= NUM_OPERATIONS; ++i) {
    uint64_t price = 900 + (i % 200);
    Side side = (i & 1) ? Side::Buy : Side::Sell;

    if (side == Side::Buy) {
      book->add_order<Side::Buy>(i, price, 10);
    } else {
      book->add_order<Side::Sell>(i, price, 10);
    }

    if (i > 5000) {
      uint64_t cancel_id = i - 4999;
      book->cancel_order(cancel_id);
    }
  }
  return 0;
}
