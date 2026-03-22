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

  // Shared search kernel used by both find_or_create_level and
  // remove_empty_level. Returns {existing_level_or_nullptr, insertion_index}.
  // When the level is found, first = the level and second = its index.
  // When not found, first = nullptr and second = where it would be inserted.
  template <Side S>
  [[nodiscard]] std::pair<PriceLevel*, size_t> find_level_or_pos(
      uint64_t price) const {
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;

    if (count == 0) return {nullptr, 0};

    if (count <= kLinearScanThreshold) {
      // Linear scan: predictable sequential branches, no misprediction cost.
      // Buy levels are sorted descending; ask levels ascending.
      for (size_t i = 0; i < count; ++i) {
        uint64_t lp = levels[i]->price;
        if (lp == price) return {levels[i], i};
        bool past_insertion_point;
        if constexpr (S == Side::Buy)
          past_insertion_point = (lp < price);
        else
          past_insertion_point = (lp > price);
        if (past_insertion_point) return {nullptr, i};
      }
      return {nullptr, count};
    }

    // Binary search for larger level counts.
    size_t L = 0, R = count - 1;
    while (L <= R) {
      size_t mid = L + ((R - L) / 2);
      uint64_t mid_price = levels[mid]->price;
      if (mid_price == price) return {levels[mid], mid};
      bool move_right;
      if constexpr (S == Side::Buy)
        move_right = (mid_price > price);
      else
        move_right = (mid_price < price);
      if (move_right) {
        L = mid + 1;
      } else {
        if (mid == 0) return {nullptr, 0};
        R = mid - 1;
      }
    }
    return {nullptr, L};
  }

  // Removes a known-empty level from the sorted pointer array.
  // Called only when level->empty() is true, which is [[unlikely]] — most
  // cancels remove one order from a multi-order level.
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
    // genuinely uncommon in a live book; [[unlikely]] is correct here.
    if (level->empty()) [[unlikely]] {
      remove_empty_level<S>(price);
      level_pool.deallocate(level);
    }
    order_pool.deallocate(o);
    order_lookup[id] = nullptr;
  }

 public:
  template <Side S>
  [[nodiscard]] size_t get_level_count() const {
    return (S == Side::Buy) ? bid_count : ask_count;
  }

  template <Side S>
  [[nodiscard]] const PriceLevel* get_level(size_t index) const {
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    if (index >= count) [[unlikely]]
      return nullptr;
    return levels[index];
  }

  [[nodiscard]] const Order* get_order(uint64_t id) const {
    if (id >= MAX_ORDERS) [[unlikely]]
      return nullptr;
    return order_lookup[id];
  }

  [[nodiscard]] size_t get_active_order_count() const {
    return order_pool.size();
  }

  [[nodiscard]] size_t get_active_level_count() const {
    return level_pool.size();
  }

  template <Side S>
  PriceLevel* find_or_create_level(uint64_t price) {
    auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    auto& count = (S == Side::Buy) ? bid_count : ask_count;

    if (count >= MAX_LEVELS) [[unlikely]]
      return nullptr;

    auto [existing, pos] = find_level_or_pos<S>(price);
    if (existing) return existing;

    // Shift everything right to make room at pos, then insert.
    if (pos < count) {
      std::memmove(&levels[pos + 1], &levels[pos],
                   (count - pos) * sizeof(PriceLevel*));
    }
    PriceLevel* p = level_pool.allocate();
    if (!p) [[unlikely]]
      return nullptr;
    p->price = price;
    levels[pos] = p;
    count++;
    return p;
  }

  template <Side S>
  std::expected<void, Error> add_order(uint64_t id, uint64_t price,
                                       uint32_t quantity) {
    if (id >= MAX_ORDERS) [[unlikely]]
      return std::unexpected(Error::InvalidOrderId);
    if (order_lookup[id] != nullptr) [[unlikely]]
      return std::unexpected(Error::DuplicateOrderId);
    PriceLevel* p = find_or_create_level<S>(price);
    if (p == nullptr) [[unlikely]]
      return std::unexpected(Error::PoolExhausted);
    Order* o = order_pool.allocate();
    if (o == nullptr) [[unlikely]]
      return std::unexpected(Error::PoolExhausted);
    o->id = id;
    o->price = price;
    o->quantity = quantity;
    o->side = S;
    o->level = p;
    p->push_back(o);
    order_lookup[id] = o;
    return {};
  }

  std::expected<void, Error> cancel_order(uint64_t id) {
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) [[unlikely]]
      return std::unexpected(Error::InvalidOrderId);
    Order* o = order_lookup[id];
    if (o->side == Side::Buy)
      cancel_order_impl<Side::Buy>(o);
    else
      cancel_order_impl<Side::Sell>(o);
    return {};
  }

  std::expected<void, Error> modify_order(uint64_t id, uint64_t new_price,
                                          uint32_t new_quantity) {
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) [[unlikely]]
      return std::unexpected(Error::InvalidOrderId);
    if (new_quantity == 0) [[unlikely]]
      return cancel_order(id);
    Order* o = order_lookup[id];
    // Fast path: same price, quantity decreasing. In-place update, no
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
    } else {
      cancel_order_impl<Side::Sell>(o);
      return add_order<Side::Sell>(id, new_price, new_quantity);
    }
  }

  template <Side S>
  uint32_t match(uint64_t incoming_id, uint64_t incoming_price,
                 uint32_t incoming_qty) {
    auto& levels = (S == Side::Buy) ? ask_levels : bid_levels;
    auto& count = (S == Side::Buy) ? ask_count : bid_count;

    while (incoming_qty > 0 && count > 0) {
      PriceLevel* best_level = levels[0];
      uint64_t best_price = best_level->price;

      if constexpr (S == Side::Buy) {
        if (incoming_price < best_price) break;
      } else {
        if (incoming_price > best_price) break;
      }

      Order* resting = best_level->head;
      while (resting != nullptr && incoming_qty > 0) {
        uint32_t trade_qty = std::min(incoming_qty, resting->quantity);
        if (trade_count < trade_buffer.size()) [[likely]] {
          trade_buffer[trade_count++] = {incoming_id, resting->id, best_price,
                                         trade_qty};
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
  std::expected<void, Error> execute_order(uint64_t id, uint64_t price,
                                           uint32_t quantity) {
    if (id >= MAX_ORDERS) [[unlikely]]
      return std::unexpected(Error::InvalidOrderId);
    if (order_lookup[id] != nullptr) [[unlikely]]
      return std::unexpected(Error::DuplicateOrderId);
    uint32_t remaining_qty = match<S>(id, price, quantity);
    if (remaining_qty > 0) return add_order<S>(id, price, remaining_qty);
    return {};
  }

  [[nodiscard]] const PriceLevel* get_best_bid() const {
    if (bid_count == 0) [[unlikely]]
      return nullptr;
    return bid_levels[0];
  }

  [[nodiscard]] const PriceLevel* get_best_ask() const {
    if (ask_count == 0) [[unlikely]]
      return nullptr;
    return ask_levels[0];
  }

  template <Side S>
  size_t get_l2_snapshot(LevelInfo* out_buffer, size_t max_depth) const {
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;
    size_t depth = std::min(max_depth, count);
    for (size_t i = 0; i < depth; ++i)
      out_buffer[i] = {levels[i]->price, levels[i]->total_quantity};
    return depth;
  }

  size_t drain_trades(Trade* out, size_t max) {
    size_t n = std::min(trade_count, max);
    std::memcpy(out, trade_buffer.data(), n * sizeof(Trade));
    trade_count = 0;
    return n;
  }

  [[nodiscard]] size_t get_trade_count() const { return trade_count; }
};
