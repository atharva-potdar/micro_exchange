#include <benchmark/benchmark.h>

#include <memory>

#include "orderbook/orderbook.hpp"

// 1. Hot Path: Adding orders to an ALREADY EXISTING price level
static void BM_AddOrder_ExistingLevel(benchmark::State& state) {
  OrderBook<1024, 1024> b;

  if (!b.add_order<Side::Buy>(1, 100, 10).has_value()) [[unlikely]] {
    state.SkipWithError("Initial add failed");
    return;
  }

  uint64_t id = 2;
  for (auto _ : state) {
    // When we reach 500, pause the timer and clean up the orders.
    // This allows the benchmark to run endlessly without hitting MAX_ORDERS.
    if (id > 500) [[unlikely]] {
      state.PauseTiming();
      for (uint64_t j = 2; j <= 500; ++j) {
        if (b.cancel_order(j).has_value() != true) [[unlikely]] {
          state.SkipWithError("Cancel failed during reset");
          return;
        }
      }
      id = 2;
      state.ResumeTiming();
    }

    // Measure EXACTLY one addition per benchmark iteration
    auto result = b.add_order<Side::Buy>(id, 100, 10);
    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Add order failed during loop");
      return;
    }

    benchmark::DoNotOptimize(result);
    id++;
  }
}
BENCHMARK(BM_AddOrder_ExistingLevel);

// 2. Cold Path (Best Case): Creating NEW price levels at the END of the array
// Since Side::Buy sorts descending, inserting sequentially lower prices (1000 -
// i) forces the binary search to append to the back, resulting in a 0-byte
// std::memmove.
static void BM_AddOrder_NewLevels_BestCase(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  uint64_t i = 1;

  for (auto _ : state) {
    // When we reach 500 levels, pause the timer and completely reset the book
    if (i > 500) [[unlikely]] {
      state.PauseTiming();
      b = std::make_unique<OrderBook<1024, 1024>>();
      i = 1;
      state.ResumeTiming();
    }

    // 1000 - i generates DESCENDING prices (999, 998, 997...)
    // For a Buy book, these naturally append to the END of the array.
    auto result = b->add_order<Side::Buy>(i, 1000 - i, 10);

    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Add order failed");
      return;
    }
    benchmark::DoNotOptimize(result);
    i++;
  }
}
BENCHMARK(BM_AddOrder_NewLevels_BestCase);

// 3. Cold Path (Worst Case): Creating NEW price levels at the FRONT of the
// array Since Side::Buy sorts descending, inserting sequentially higher prices
// (1000 + i) forces the binary search to index 0, triggering a full
// std::memmove of the entire array.
static void BM_AddOrder_NewLevels_WorstCase(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  uint64_t i = 1;

  for (auto _ : state) {
    if (i > 500) [[unlikely]] {
      state.PauseTiming();
      b = std::make_unique<OrderBook<1024, 1024>>();
      i = 1;
      state.ResumeTiming();
    }

    // 1000 + i generates ASCENDING prices (1001, 1002, 1003...)
    // For a Buy book, these force insertion at index 0, shifting everything
    // right!
    auto result = b->add_order<Side::Buy>(i, 1000 + i, 10);

    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Add order failed");
      return;
    }
    benchmark::DoNotOptimize(result);
    i++;
  }
}
BENCHMARK(BM_AddOrder_NewLevels_WorstCase);
