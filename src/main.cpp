// src/main.cpp
// High-performance benchmark generator for micro_exchange

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "orderbook/orderbook.hpp"

// [Compiler Barrier]
template <class T>
__attribute__((always_inline)) inline void do_not_optimize(T const& val) {
  asm volatile("" : : "r,m"(val) : "memory");
}

// [PRNG]
struct XorShift64 {
  uint64_t s;
  constexpr explicit XorShift64(uint64_t seed) : s{seed == 0 ? 1 : seed} {}
  inline uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
};

// [Fast Math]
inline uint32_t fast_range32(uint64_t random_val, uint32_t range) {
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(static_cast<uint32_t>(random_val)) * range) >> 32);
}

// [Global Engine Configuration]
constexpr size_t MAX_ORDERS = 4'000'000;
constexpr size_t MAX_LEVELS = 50'000;
static OrderBook<MAX_ORDERS, MAX_LEVELS> book;

// [Optimized Order Tracking]
// 2^18 elements * 24 bytes = ~6.2MB (Fits beautifully in L3 Cache)
struct TrackedOrder {
  uint64_t id;
  uint64_t price;
  uint32_t qty;
};

constexpr size_t RING_SIZE = 262144;
constexpr size_t RING_MASK = RING_SIZE - 1;
static std::array<TrackedOrder, RING_SIZE> order_ring{};
size_t ring_head = 0;

inline void track_order(uint64_t id, uint64_t price, uint32_t qty) {
  order_ring[ring_head] = {id, price, qty};
  ring_head = (ring_head + 1) & RING_MASK;
}

// Pick ONE order. No retry loops. No probing the 32MB engine arrays.
inline TrackedOrder* get_random_target(XorShift64& rng) {
  uint32_t idx = static_cast<uint32_t>(rng.next()) & RING_MASK;
  return &order_ring[idx];
}

// [ID Management]
bool has_wrapped = false;
uint64_t next_id = 1;

inline uint64_t alloc_id() {
  uint64_t id = next_id++;
  if (next_id >= MAX_ORDERS) {
    next_id = 1;
    has_wrapped = true;
  }
  if (has_wrapped) {
    (void)book.cancel_order(id);
  }
  return id;
}

//[Engine Operations]
void do_add(XorShift64& rng, uint64_t mid, uint64_t spread) {
  uint64_t id = alloc_id();
  uint32_t qty = 1 + fast_range32(rng.next(), 100);

  uint32_t half_spread = static_cast<uint32_t>(spread / 2);
  uint32_t offset = fast_range32(rng.next(), half_spread) +
                    fast_range32(rng.next(), half_spread);
  uint64_t price;

  if (rng.next() & 1) {  // Buy
    price = mid > offset ? mid - offset : 1;
    (void)book.add_order<Side::Buy>(id, price, qty);
  } else {  // Sell
    price = mid + offset;
    (void)book.add_order<Side::Sell>(id, price, qty);
  }
  track_order(id, price, qty);
}

void do_cancel(XorShift64& rng) {
  TrackedOrder* target = get_random_target(rng);
  if (target->id) {
    // Engine's native error handling is faster than us double-checking
    (void)book.cancel_order(target->id);
    target->id = 0;
  }
}

void do_modify(XorShift64& rng, uint64_t mid, uint64_t spread) {
  TrackedOrder* target = get_random_target(rng);
  if (!target->id) return;

  uint32_t r = fast_range32(rng.next(), 100);
  if (r < 60) {
    // FAST path: reduce qty
    uint32_t new_qty = target->qty > 1 ? target->qty / 2 : 1;
    if (book.modify_order(target->id, target->price, new_qty)) {
      target->qty = new_qty;
    }
  } else if (r < 85) {
    // SLOW path: increase qty
    uint32_t new_qty = target->qty + 10;
    if (book.modify_order(target->id, target->price, new_qty)) {
      target->qty = new_qty;
    }
  } else {
    // SLOW path: change price
    uint32_t half_spread = static_cast<uint32_t>(spread / 2);
    uint32_t offset = fast_range32(rng.next(), half_spread) +
                      fast_range32(rng.next(), half_spread);
    uint64_t new_price;

    if (target->price <= mid) {  // Treat as Buy side
      new_price = mid > offset ? mid - offset : 1;
    } else {  // Treat as Sell side
      new_price = mid + offset;
    }

    if (book.modify_order(target->id, new_price, target->qty)) {
      target->price = new_price;
    }
  }
}

void do_execute(XorShift64& rng) {
  uint64_t id = alloc_id();
  uint32_t qty = 1 + fast_range32(rng.next(), 20);

  if (rng.next() & 1) {  // Buy Execute
    auto best = book.get_best_ask();
    if (!best) return;
    uint64_t exec_price = best->price + 2;
    (void)book.execute_order<Side::Buy>(id, exec_price, qty);
    track_order(id, exec_price, qty);
  } else {  // Sell Execute
    auto best = book.get_best_bid();
    if (!best) return;
    uint64_t exec_price = best->price > 2 ? best->price - 2 : 1;
    (void)book.execute_order<Side::Sell>(id, exec_price, qty);
    track_order(id, exec_price, qty);
  }
}

inline uint8_t pick_op(XorShift64& rng, const uint8_t weights[5]) {
  uint32_t r = fast_range32(rng.next(), 100);
  for (uint8_t i = 0; i < 5; ++i) {
    if (r < weights[i]) return i;
  }
  return 4;
}

uint64_t total_trades_global = 0;

void run_phase(const char* name, size_t ops, const uint8_t weights[5],
               uint64_t mid, uint64_t spread) {
  XorShift64 rng(1337 + ops);
  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < ops; ++i) {
    uint8_t op = pick_op(rng, weights);
    switch (op) {
      case 0:
        do_add(rng, mid, spread);
        break;
      case 1:
        do_cancel(rng);
        break;
      case 2:
        do_modify(rng, mid, spread);
        break;
      case 3:
        do_execute(rng);
        break;
      case 4: {
        LevelInfo bids[20], asks[20];
        size_t nb = book.get_l2_snapshot<Side::Buy>(bids, 20);
        size_t na = book.get_l2_snapshot<Side::Sell>(asks, 20);

        do_not_optimize(nb);
        do_not_optimize(na);
        if (nb > 0) do_not_optimize(bids[0]);
        if (na > 0) do_not_optimize(asks[0]);
        break;
      }
    }

    if (i % 100'000 == 0) {
      Trade trades[512];
      size_t n = book.drain_trades(trades, 512);
      total_trades_global += n;
      if (n > 0) do_not_optimize(trades[0]);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;
  std::cout << "Phase: " << std::left << std::setw(10) << name
            << " | Ops: " << std::setw(10) << ops << " | Time: " << std::fixed
            << std::setprecision(3) << diff.count() << " s"
            << " | Rate: " << (static_cast<double>(ops) / diff.count() / 1e6)
            << " M ops/s\n";
}

int main() {
  std::cout << "=================================================\n";
  std::cout << "   Micro-Exchange Perf Workload Generator\n";
  std::cout << "=================================================\n";
  std::cout << "Configuration:\n";
  std::cout << "  MAX_ORDERS: " << MAX_ORDERS << "\n";
  std::cout << "  MAX_LEVELS: " << MAX_LEVELS << "\n";
  std::cout << "  Tracked:    " << RING_SIZE << "\n\n";

  const uint8_t WARMUP_WEIGHTS[] = {100, 100, 100, 100, 100};
  const uint8_t QUIET_WEIGHTS[] = {55, 85, 93, 95, 100};
  const uint8_t ACTIVE_WEIGHTS[] = {30, 65, 75, 95, 100};
  const uint8_t VOLATILE_WEIGHTS[] = {20, 60, 75, 95, 100};

  std::cout << "Starting benchmark phases...\n";

  run_phase("WARMUP", 500'000, WARMUP_WEIGHTS, 100'000, 2'000);
  run_phase("QUIET", 10'000'000, QUIET_WEIGHTS, 100'000, 1'000);
  run_phase("ACTIVE", 25'000'000, ACTIVE_WEIGHTS, 100'000, 1'500);
  run_phase("VOLATILE", 15'000'000, VOLATILE_WEIGHTS, 100'000, 4'000);

  Trade trades[512];
  while (size_t n = book.drain_trades(trades, 512)) {
    total_trades_global += n;
  }

  auto best_bid = book.get_best_bid();
  auto best_ask = book.get_best_ask();

  std::cout << "\n--- Benchmark Complete ---\n";
  std::cout << "Total operations:  " << 50'500'000 << "\n";
  std::cout << "Total trades:      " << total_trades_global << "\n";
  std::cout << "Final best bid:    " << (best_bid ? best_bid->price : 0)
            << "\n";
  std::cout << "Final best ask:    " << (best_ask ? best_ask->price : 0)
            << "\n";
  std::cout << "Final Spread:      "
            << (best_ask && best_bid ? (best_ask->price - best_bid->price) : 0)
            << "\n";

  return 0;
}
