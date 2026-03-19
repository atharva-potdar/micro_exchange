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

// 4. Modify Fast Path: Decreasing quantity (Retains Time Priority)
// Expected to be extremely fast (~1-2ns) as it is an O(1) in-place update.
static void BM_ModifyOrder_FastPath(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  uint64_t id = 1;
  uint32_t qty = 1000000;

  if (!b->add_order<Side::Buy>(id, 100, qty).has_value()) [[unlikely]] {
    state.SkipWithError("Initial add failed");
    return;
  }

  for (auto _ : state) {
    qty--;  // Decreasing quantity hits the fast path

    // Reset when we hit 0 to prevent accidental cancellation
    if (qty == 0) [[unlikely]] {
      state.PauseTiming();

      if (!b->cancel_order(id).has_value()) [[unlikely]] {
        state.SkipWithError("Cancel failed during reset");
        return;
      }

      qty = 1000000;

      if (!b->add_order<Side::Buy>(id, 100, qty).has_value()) [[unlikely]] {
        state.SkipWithError("Add failed during reset");
        return;
      }

      state.ResumeTiming();
      qty--;  // Decrement so the timed code never modifies to exactly 1000000
    }

    auto result = b->modify_order(id, 100, qty);
    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Modify order failed during loop");
      return;
    }

    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_ModifyOrder_FastPath);

// 5. Modify Slow Path: Increasing quantity (Loses Time Priority)
// Expected to be slower (~5-8ns) as it triggers a full cancel and re-add to the
// tail.
static void BM_ModifyOrder_SlowPath(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  uint64_t id = 1;
  uint32_t qty = 1;

  if (!b->add_order<Side::Buy>(id, 100, qty).has_value()) [[unlikely]] {
    state.SkipWithError("Initial add failed");
    return;
  }

  for (auto _ : state) {
    qty++;  // Increasing quantity forces the slow path (cancel + add)

    // Prevent integer overflow during endless benchmark loops
    if (qty > 1000000) [[unlikely]] {
      state.PauseTiming();

      if (!b->cancel_order(id).has_value()) [[unlikely]] {
        state.SkipWithError("Cancel failed during reset");
        return;
      }

      qty = 1;

      if (!b->add_order<Side::Buy>(id, 100, qty).has_value()) [[unlikely]] {
        state.SkipWithError("Add failed during reset");
        return;
      }

      state.ResumeTiming();
      qty++;  // Increment so the timed code starts at modifying to 2
    }

    auto result = b->modify_order(id, 100, qty);
    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Modify order failed during loop");
      return;
    }

    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_ModifyOrder_SlowPath);

// 6. Execute Hot Path: Partial Fills (Pure Math & Cache Updates)
// An incoming order eats a piece of a massive resting order.
// No orders or levels are deallocated.
static void BM_ExecuteOrder_PartialFill(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  uint64_t resting_id = 1;
  uint32_t starting_qty = 1000000;

  if (!b->add_order<Side::Sell>(resting_id, 100, starting_qty).has_value())
      [[unlikely]] {
    state.SkipWithError("Initial add failed");
    return;
  }

  uint32_t iterations = 0;
  uint64_t aggressor_id = 2;

  for (auto _ : state) {
    // Reset every 500 iterations to avoid trade_buffer overflow
    if (iterations >= 500) [[unlikely]] {
      state.PauseTiming();
      b = std::make_unique<OrderBook<1024, 1024>>();

      if (!b->add_order<Side::Sell>(resting_id, 100, starting_qty).has_value())
          [[unlikely]] {
        state.SkipWithError("Add failed during reset");
        return;
      }

      iterations = 0;
      state.ResumeTiming();
    }

    // Match exactly 1 lot to trigger a partial fill
    auto result = b->execute_order<Side::Buy>(aggressor_id, 100, 1);

    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Execute order failed");
      return;
    }

    benchmark::DoNotOptimize(result);
    iterations++;
  }
}
BENCHMARK(BM_ExecuteOrder_PartialFill);

// 7. Execute Cold Path: Full Fill, Order GC, and Level GC (std::memmove)
// An incoming order sweeps the Best Ask, destroying the order AND the
// PriceLevel.
static void BM_ExecuteOrder_TradeAndShift(benchmark::State& state) {
  auto b = std::make_unique<OrderBook<1024, 1024>>();
  const uint32_t MAX_SELLERS = 500;

  // Setup 500 distinct ask levels
  for (uint32_t i = 0; i < MAX_SELLERS; ++i) {
    if (!b->add_order<Side::Sell>(i + 1, 100 + i, 10).has_value())
        [[unlikely]] {
      state.SkipWithError("Initial add failed");
      return;
    }
  }

  uint32_t remaining_sellers = MAX_SELLERS;
  uint64_t aggressor_id = 1000;

  for (auto _ : state) {
    // Reset when all 500 sellers are consumed
    if (remaining_sellers == 0) [[unlikely]] {
      state.PauseTiming();
      b = std::make_unique<OrderBook<1024, 1024>>();

      for (uint32_t i = 0; i < MAX_SELLERS; ++i) {
        if (!b->add_order<Side::Sell>(i + 1, 100 + i, 10).has_value())
            [[unlikely]] {
          state.SkipWithError("Add failed during reset");
          return;
        }
      }

      remaining_sellers = MAX_SELLERS;
      state.ResumeTiming();
    }

    // Sweep the Best Ask, forcing order and level deletion
    auto result = b->execute_order<Side::Buy>(aggressor_id, 2000, 10);

    if (!result.has_value()) [[unlikely]] {
      state.SkipWithError("Execute order failed");
      return;
    }

    benchmark::DoNotOptimize(result);
    remaining_sellers--;
  }
}
BENCHMARK(BM_ExecuteOrder_TradeAndShift);
