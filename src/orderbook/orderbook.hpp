#pragma once
#include <array>
#include <cstring>
#include <expected>
#include <utility>

#include "memory/arena.hpp"
#include "orderbook/types.hpp"

template <size_t MAX_ORDERS, size_t MAX_LEVELS>
class OrderBook {
 private:
  Arena<Order, MAX_ORDERS> order_pool;
  Arena<PriceLevel, MAX_LEVELS> level_pool;
  std::array<PriceLevel*, MAX_LEVELS> bid_levels{};
  size_t bid_count = 0;
  std::array<PriceLevel*, MAX_LEVELS> ask_levels{};
  size_t ask_count = 0;
  std::array<Order*, MAX_ORDERS> order_lookup{};
  std::array<Trade, 512> trade_buffer{};
  size_t trade_count = 0;

  // Below this threshold, linear scan beats binary search: branches are
  // predictable (sequential), access is cache-sequential, and there's no
  // log2(n) misprediction cost. 16 pointers = 128 bytes = 2 cache lines.
  static constexpr size_t kLinearScanThreshold = 16;

  template <Side S>
  [[nodiscard]] auto find_level_or_pos(uint64_t price) const
      -> std::pair<PriceLevel*, size_t> {
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;

    if (count == 0) {
      return {nullptr, 0};
    }

    if (count <= kLinearScanThreshold) {
      // Buy levels are sorted descending; ask levels ascending.
      for (size_t i = 0; i < count; ++i) {
        uint64_t lp = levels[i]->price;
        if (lp == price) {
          return {levels[i], i};
        }
        bool past_insertion_point = false;
        if constexpr (S == Side::Buy) {
          past_insertion_point = (lp < price);
        } else {
          past_insertion_point = (lp > price);
        }
        if (past_insertion_point) {
          return {nullptr, i};
        }
      }
      return {nullptr, count};
    }

    size_t L = 0;
    size_t R = count - 1;
    while (L <= R) {
      size_t mid = L + ((R - L) / 2);
      uint64_t mid_price = levels[mid]->price;
      if (mid_price == price) {
        return {levels[mid], mid};
      }
      bool move_right = false;
      if constexpr (S == Side::Buy) {
        move_right = (mid_price > price);
      } else {
        move_right = (mid_price < price);
      }
      if (move_right) {
        L = mid + 1;
      } else {
        if (mid == 0) {
          return {nullptr, 0};
        }
        R = mid - 1;
      }
    }
    return {nullptr, L};
  }

  template <Side S>
  void remove_empty_level(uint64_t price) {
    auto& level_array = (S == Side::Buy) ? bid_levels : ask_levels;
    size_t& current_count = (S == Side::Buy) ? bid_count : ask_count;

    auto [found, i] = find_level_or_pos<S>(price);
    if (found) [[likely]] {
      if (i < current_count - 1) {
        std::memmove(&level_array[i], &level_array[i + 1],
                     (current_count - i - 1) * sizeof(PriceLevel*));
      }
      current_count--;
    }
  }

  template <Side S>
  void cancel_order_impl(Order* o) {
    PriceLevel* level = o->level;
    uint64_t price = o->price;
    uint64_t id = o->id;
    level->remove(o);
    // Most cancels leave other orders at this level. Level destruction is
    // genuinely uncommon in a live book.
    if (level->empty()) [[unlikely]] {
      remove_empty_level<S>(price);
      level_pool.deallocate(level);
    }
    order_pool.deallocate(o);
    order_lookup[id] = nullptr;
  }

 public:
  template <Side S>
  [[nodiscard]] auto get_level_count() const -> size_t {
    return (S == Side::Buy) ? bid_count : ask_count;
  }

  template <Side S>
  [[nodiscard]] auto get_level(size_t index) const -> const PriceLevel* {
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    if (index >= count) {
      [[unlikely]] return nullptr;
    }
    return levels[index];
  }

  [[nodiscard]] auto get_order(uint64_t id) const -> const Order* {
    if (id >= MAX_ORDERS) {
      [[unlikely]] return nullptr;
    }
    return order_lookup[id];
  }

  [[nodiscard]] auto get_active_order_count() const -> size_t {
    return order_pool.size();
  }

  [[nodiscard]] auto get_active_level_count() const -> size_t {
    return level_pool.size();
  }

  template <Side S>
  auto find_or_create_level(uint64_t price) -> PriceLevel* {
    auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    auto& count = (S == Side::Buy) ? bid_count : ask_count;

    if (count >= MAX_LEVELS) {
      [[unlikely]] return nullptr;
    }

    auto [existing, pos] = find_level_or_pos<S>(price);
    if (existing) {
      return existing;
    }

    if (pos < count) {
      std::memmove(&levels[pos + 1], &levels[pos],
                   (count - pos) * sizeof(PriceLevel*));
    }
    PriceLevel* p = level_pool.allocate();
    if (!p) {
      [[unlikely]] return nullptr;
    }
    p->price = price;
    levels[pos] = p;
    count++;
    return p;
  }

  template <Side S>
  auto add_order(uint64_t id, uint64_t price, uint32_t quantity)
      -> std::expected<void, Error> {
    if (id >= MAX_ORDERS) {
      [[unlikely]] return std::unexpected(Error::InvalidOrderId);
    }
    if (order_lookup[id] != nullptr) {
      [[unlikely]] return std::unexpected(Error::DuplicateOrderId);
    }
    PriceLevel* p = find_or_create_level<S>(price);
    if (p == nullptr) {
      [[unlikely]] return std::unexpected(Error::PoolExhausted);
    }
    Order* o = order_pool.allocate();
    if (o == nullptr) {
      [[unlikely]] return std::unexpected(Error::PoolExhausted);
    }
    o->id = id;
    o->price = price;
    o->quantity = quantity;
    o->side = S;
    o->level = p;
    p->push_back(o);
    order_lookup[id] = o;
    return {};
  }

  auto cancel_order(uint64_t id) -> std::expected<void, Error> {
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) {
      [[unlikely]] return std::unexpected(Error::InvalidOrderId);
    }
    Order* o = order_lookup[id];
    if (o->side == Side::Buy) {
      cancel_order_impl<Side::Buy>(o);
    } else {
      cancel_order_impl<Side::Sell>(o);
    }
    return {};
  }

  auto modify_order(uint64_t id, uint64_t new_price, uint32_t new_quantity)
      -> std::expected<void, Error> {
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) {
      [[unlikely]] return std::unexpected(Error::InvalidOrderId);
    }
    if (new_quantity == 0) {
      [[unlikely]] return cancel_order(id);
    }
    Order* o = order_lookup[id];
    // Fast path: same price, quantity decreasing — in-place update, no
    // structural change to any level or the sorted arrays.
    if (new_price == o->price && new_quantity <= o->quantity) [[likely]] {
      o->level->total_quantity -= (o->quantity - new_quantity);
      o->quantity = new_quantity;
      return {};
    }
    // Slow path: price change or quantity increase requires cancel + re-add.
    Side side = o->side;
    if (side == Side::Buy) {
      cancel_order_impl<Side::Buy>(o);
      return add_order<Side::Buy>(id, new_price, new_quantity);
    }
    cancel_order_impl<Side::Sell>(o);
    return add_order<Side::Sell>(id, new_price, new_quantity);
  }

  template <Side S>
  auto match(uint64_t incoming_id, uint64_t incoming_price,
             uint32_t incoming_qty) -> uint32_t {
    auto& levels = (S == Side::Buy) ? ask_levels : bid_levels;
    auto& count = (S == Side::Buy) ? ask_count : bid_count;

    while (incoming_qty > 0 && count > 0) {
      PriceLevel* best_level = levels[0];
      uint64_t best_price = best_level->price;

      if constexpr (S == Side::Buy) {
        if (incoming_price < best_price) {
          break;
        }
      } else {
        if (incoming_price > best_price) {
          break;
        }
      }

      Order* resting = best_level->head;
      while (resting != nullptr && incoming_qty > 0) {
        uint32_t trade_qty = std::min(incoming_qty, resting->quantity);
        if (trade_count < trade_buffer.size()) [[likely]] {
          trade_buffer[trade_count++] = {.buyer_order_id = incoming_id,
                                         .seller_order_id = resting->id,
                                         .price = best_price,
                                         .quantity = trade_qty};
        }
        incoming_qty -= trade_qty;
        resting->quantity -= trade_qty;
        best_level->total_quantity -= trade_qty;
        Order* next_resting = resting->next;
        if (resting->quantity == 0) {
          uint64_t resting_id = resting->id;
          best_level->remove(resting);
          order_pool.deallocate(resting);
          order_lookup[resting_id] = nullptr;
        }
        resting = next_resting;
      }

      if (best_level->empty()) [[unlikely]] {
        if (count > 1) {
          std::memmove(&levels[0], &levels[1],
                       (count - 1) * sizeof(PriceLevel*));
        }
        count--;
        level_pool.deallocate(best_level);
      }
    }
    return incoming_qty;
  }

  template <Side S>
  auto execute_order(uint64_t id, uint64_t price, uint32_t quantity)
      -> std::expected<void, Error> {
    if (id >= MAX_ORDERS) {
      [[unlikely]] return std::unexpected(Error::InvalidOrderId);
    }
    if (order_lookup[id] != nullptr) {
      [[unlikely]] return std::unexpected(Error::DuplicateOrderId);
    }
    uint32_t remaining_qty = match<S>(id, price, quantity);
    if (remaining_qty > 0) {
      return add_order<S>(id, price, remaining_qty);
    }
    return {};
  }

  [[nodiscard]] auto get_best_bid() const -> const PriceLevel* {
    if (bid_count == 0) {
      [[unlikely]] return nullptr;
    }
    return bid_levels[0];
  }

  [[nodiscard]] auto get_best_ask() const -> const PriceLevel* {
    if (ask_count == 0) {
      [[unlikely]] return nullptr;
    }
    return ask_levels[0];
  }

  template <Side S>
  auto get_l2_snapshot(LevelInfo* out_buffer, size_t max_depth) const
      -> size_t {
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;
    size_t depth = std::min(max_depth, count);
    for (size_t i = 0; i < depth; ++i) {
      out_buffer[i] = {levels[i]->price, levels[i]->total_quantity};
    }
    return depth;
  }

  auto drain_trades(Trade* out, size_t max) -> size_t {
    size_t n = std::min(trade_count, max);
    std::memcpy(out, trade_buffer.data(), n * sizeof(Trade));
    trade_count = 0;
    return n;
  }

  [[nodiscard]] auto get_trade_count() const -> size_t { return trade_count; }
};
