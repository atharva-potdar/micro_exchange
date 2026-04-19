#include <ctime>
#include <x86intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <print>
#include <vector>

#include "orderbook/orderbook.hpp"

static auto tsc_to_ns_factor() -> double {
  __rdtsc();
  struct timespec t0{}, t1{};
  clock_gettime(CLOCK_MONOTONIC, &t0);
  uint64_t c0 = __rdtsc();
  volatile uint64_t sink = 0;
  for (uint64_t i = 0; i < 10'000'000; ++i) sink += i;
  uint64_t c1 = __rdtsc();
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double ns =
      static_cast<double>(t1.tv_sec - t0.tv_sec) * 1e9 + static_cast<double>(t1.tv_nsec - t0.tv_nsec);
  auto ticks = static_cast<double>(c1 - c0);
  return ns / ticks;
}

static auto rdtsc_overhead() -> uint64_t {
  uint64_t min_overhead = UINT64_MAX;
  for (int i = 0; i < 1000; ++i) {
    uint64_t a = __rdtsc();
    uint64_t b = __rdtsc();
    uint64_t d = b - a;
    if (d < min_overhead) min_overhead = d;
  }
  return min_overhead;
}

struct Stats {
  double mean, p50, p99, p999, p9999, p100;
};

// Op signature: void op(bool& did_reset)
// Set did_reset = true when a reset fires; that iteration is excluded.
template <typename Op>
auto measure(Op&& op, size_t n_samples = 100'000, size_t warmup = 10'000) -> Stats {
  static const double ns_per_tick = tsc_to_ns_factor();
  static const uint64_t overhead = rdtsc_overhead();

  for (size_t i = 0; i < warmup; ++i) {
    bool reset = false;
    op(reset);
  }

  std::vector<uint64_t> samples;
  samples.reserve(n_samples);

  while (samples.size() < n_samples) {
    bool did_reset = false;
    uint64_t t0 = __rdtsc();
    op(did_reset);
    uint64_t t1 = __rdtsc();
    if (!did_reset) {
      uint64_t delta = (t1 > t0 + overhead) ? (t1 - t0 - overhead) : 0;
      samples.push_back(delta);
    }
  }

  std::ranges::sort(samples);

  auto pct = [&](double p) -> double {
    auto idx = static_cast<size_t>(p * static_cast<double>(n_samples - 1));
    return static_cast<double>(samples[idx]) * ns_per_tick;
  };

  double sum = 0;
  for (auto s : samples) sum += static_cast<double>(s);

  return {
      .mean = (sum / static_cast<double>(n_samples)) * ns_per_tick,
      .p50 = pct(0.500),
      .p99 = pct(0.990),
      .p999 = pct(0.999),
      .p9999 = pct(0.9999),
      .p100 = static_cast<double>(samples.back()) * ns_per_tick,
  };
}

static void print_header() {
  std::println("\n{:42} {:>8} {:>8} {:>8} {:>8} {:>8} {:>8}", "Benchmark", "mean", "p50", "p99",
         "p99.9", "p99.99", "max");
  std::println("{:42} {:>8} {:>8} {:>8} {:>8} {:>8} {:>8}",
         "──────────────────────────────────────────", "────────", "────────",
         "────────", "────────", "────────", "────────");
}

static void print_row(const char* name, const Stats& s) {
  std::println("{:42} {:7.2f}  {:7.2f}  {:7.2f}  {:7.2f}  {:7.2f}  {:7.2f}   (ns)", name,
         s.mean, s.p50, s.p99, s.p999, s.p9999, s.p100);
}

auto main() -> int {
  std::println("Micro-Exchange Latency Benchmarks");
  std::println("100,000 samples per benchmark, 10,000 warmup iterations");
  std::println("Reset iterations excluded from all sample sets.");
  print_header();

  {
    OrderBook<4096, 1024> book;
    book.add_order<Side::Buy>(1, 100, 10);
    uint64_t id = 2;
    constexpr uint64_t RESET_AT = 2000;

    auto s = measure([&](bool& reset) -> void {
      if (id > RESET_AT) [[unlikely]] {
        for (uint64_t j = 2; j <= RESET_AT; ++j) book.cancel_order(j);
        id = 2;
        reset = true;
        return;
      }
      auto r = book.add_order<Side::Buy>(id++, 100, 10);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("add_order (existing level)", s);
  }

  // Buy is descending: ever-lower prices append to the back (0-byte memmove).
  {
    auto book = std::make_unique<OrderBook<4096, 1024>>();
    uint64_t id = 1;
    uint64_t price = 50000;

    auto s = measure([&](bool& reset) -> void {
      if (id > 1000 || price == 0) [[unlikely]] {
        book = std::make_unique<OrderBook<4096, 1024>>();
        id = 1;
        price = 50000;
        reset = true;
        return;
      }
      auto r = book->add_order<Side::Buy>(id++, price--, 10);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("add_order (new level, best case)", s);
  }

  // Buy is descending: ever-higher prices insert at index 0 (full memmove).
  {
    auto book = std::make_unique<OrderBook<4096, 1024>>();
    uint64_t id = 1;
    uint64_t price = 1;

    auto s = measure([&](bool& reset) -> void {
      if (id > 1000) [[unlikely]] {
        book = std::make_unique<OrderBook<4096, 1024>>();
        id = 1;
        price = 1;
        reset = true;
        return;
      }
      auto r = book->add_order<Side::Buy>(id++, price++, 10);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("add_order (new level, worst case)", s);
  }

  // 3000 orders at one price; cancel first 2000, last 1000 keep the level
  // alive so remove_empty_level is never triggered.
  {
    OrderBook<4096, 1024> book;
    for (uint64_t i = 1; i <= 3000; ++i) book.add_order<Side::Buy>(i, 100, 10);
    uint64_t id = 1;

    auto s = measure([&](bool& reset) -> void {
      if (id > 2000) [[unlikely]] {
        for (uint64_t j = 1; j <= 2000; ++j)
          book.add_order<Side::Buy>(j, 100, 10);
        id = 1;
        reset = true;
        return;
      }
      auto r = book.cancel_order(id++);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("cancel_order (level survives)", s);
  }

  // 500 single-order levels; each cancel destroys the level.
  {
    auto book = std::make_unique<OrderBook<4096, 1024>>();
    constexpr uint64_t N = 500;
    auto setup = [&] -> void {
      book = std::make_unique<OrderBook<4096, 1024>>();
      for (uint64_t i = 1; i <= N; ++i)
        book->add_order<Side::Buy>(i, i * 10, 10);
    };
    setup();
    uint64_t id = 1;

    auto s = measure([&](bool& reset) -> void {
      if (id > N) [[unlikely]] {
        setup();
        id = 1;
        reset = true;
        return;
      }
      auto r = book->cancel_order(id++);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("cancel_order (level destroyed)", s);
  }

  {
    OrderBook<4096, 1024> book;
    book.add_order<Side::Buy>(1, 100, 1'000'000);
    uint32_t qty = 1'000'000;

    auto s = measure([&](bool& reset) -> void {
      if (qty <= 1) [[unlikely]] {
        book.cancel_order(1);
        qty = 1'000'000;
        book.add_order<Side::Buy>(1, 100, qty);
        reset = true;
        return;
      }
      qty--;
      auto r = book.modify_order(1, 100, qty);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("modify_order (fast path, qty dec)", s);
  }

  // Oscillates between two prices so the book never grows.
  {
    OrderBook<4096, 1024> book;
    book.add_order<Side::Buy>(1, 100, 10);
    uint64_t price = 100;

    auto s = measure([&](bool& reset) -> void {
      (void)reset;
      price = (price == 100) ? 99 : 100;
      auto r = book.modify_order(1, price, 10);
      asm volatile("" : : "r,m"(r) : "memory");
    });
    print_row("modify_order (slow path, price chg)", s);
  }

  {
    auto book = std::make_unique<OrderBook<4096, 1024>>();
    book->add_order<Side::Sell>(1, 100, 1'000'000);
    uint64_t aggressor_id = 2;
    uint32_t fills = 0;

    auto s = measure([&](bool& reset) -> void {
      if (fills >= 400) [[unlikely]] {
        book = std::make_unique<OrderBook<4096, 1024>>();
        book->add_order<Side::Sell>(1, 100, 1'000'000);
        aggressor_id = 2;
        fills = 0;
        reset = true;
        return;
      }
      if (fills % 399 == 0 && fills > 0) [[unlikely]] {
        Trade tmp[512];
        book->drain_trades(tmp, 512);
      }
      auto r = book->execute_order<Side::Buy>(aggressor_id++, 100, 1);
      asm volatile("" : : "r,m"(r) : "memory");
      fills++;
    });
    print_row("execute_order (partial fill)", s);
  }

  // Each call sweeps the best ask, destroying the order and level.
  {
    auto book = std::make_unique<OrderBook<4096, 1024>>();
    constexpr uint32_t N = 500;
    auto setup = [&] -> void {
      book = std::make_unique<OrderBook<4096, 1024>>();
      for (uint32_t i = 0; i < N; ++i)
        book->add_order<Side::Sell>(i + 1, 100 + i, 10);
    };
    setup();
    uint64_t aggressor_id = 10000;
    uint32_t remaining = N;

    auto s = measure([&](bool& reset) -> void {
      if (remaining == 0) [[unlikely]] {
        setup();
        remaining = N;
        reset = true;
        return;
      }
      Trade tmp[512];
      book->drain_trades(tmp, 512);
      auto r = book->execute_order<Side::Buy>(aggressor_id++, 99999, 10);
      asm volatile("" : : "r,m"(r) : "memory");
      remaining--;
    });
    print_row("execute_order (full fill + level GC)", s);
  }

  std::println("\nNote: max is a single-sample outlier — ignore it.");
  std::println("p99.9 is the reliable worst-case figure for resume/docs.");
  return 0;
}
