#include <array>
#include <cstring>
#include <expected>

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

  // Private templated helper for branchless level deletion
  template <Side S>
  void remove_empty_level(uint64_t price) {
    auto& level_array = (S == Side::Buy) ? bid_levels : ask_levels;
    size_t& current_count = (S == Side::Buy) ? bid_count : ask_count;

    size_t L = 0;
    size_t i = current_count;

    if (current_count > 0) {
      size_t R = current_count - 1;
      while (L <= R) {
        size_t mid = L + ((R - L) / 2);
        uint64_t mid_price = level_array[mid]->price;

        if (mid_price == price) {
          i = mid;
          break;
        }

        // Branchless side logic at compile-time!
        bool move_right;
        if constexpr (S == Side::Buy) {
          move_right = (mid_price > price);
        } else {
          move_right = (mid_price < price);
        }

        if (move_right) {
          L = mid + 1;
        } else {
          if (mid == 0) break;
          R = mid - 1;
        }
      }
    }

    if (i < current_count) [[likely]] {
      if (i < current_count - 1) {
        std::memmove(&level_array[i], &level_array[i + 1],
                     (current_count - i - 1) * sizeof(PriceLevel*));
      }
      current_count--;
    }
  }

  // Private templated implementation of cancel order
  template <Side S>
  void cancel_order_impl(Order* o) {
    PriceLevel* level = o->level;
    uint64_t price = o->price;
    uint64_t id = o->id;

    level->remove(o);

    // It's less common for an order cancel to completely delete a level
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
    if (count >= MAX_LEVELS) [[unlikely]] {
      return nullptr;
    }
    size_t i = 0;
    if (count != 0) {
      size_t R = count - 1;
      while (i <= R) {
        size_t mid = i + ((R - i) / 2);
        uint64_t mid_price = levels[mid]->price;
        if (mid_price == price) {
          return levels[mid];
        }

        bool move_right;
        if constexpr (S == Side::Buy) {
          move_right = (mid_price > price);
        } else {
          move_right = (mid_price < price);
        }

        if (move_right) {
          i = mid + 1;
        } else {
          if (mid == 0) break;
          R = mid - 1;
        }
      }
      if (i < count) {
        std::memmove(&levels[i + 1], &levels[i],
                     (count - i) * sizeof(PriceLevel*));
      }
    }

    PriceLevel* p = level_pool.allocate();
    if (!p) [[unlikely]]
      return nullptr;
    p->price = price;
    levels[i] = p;
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
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) [[unlikely]] {
      return std::unexpected(Error::InvalidOrderId);
    }

    Order* o = order_lookup[id];

    // Jump into templated logic to eliminate downstream branching
    if (o->side == Side::Buy) {
      cancel_order_impl<Side::Buy>(o);
    } else {
      cancel_order_impl<Side::Sell>(o);
    }

    return {};
  }
  std::expected<void, Error> modify_order(uint64_t id, uint64_t new_price,
                                          uint32_t new_quantity) {
    if (id >= MAX_ORDERS || order_lookup[id] == nullptr) [[unlikely]] {
      return std::unexpected(Error::InvalidOrderId);
    }

    if (new_quantity == 0) [[unlikely]] {
      return cancel_order(id);
    }

    Order* o = order_lookup[id];

    // FAST PATH: Same price, decreasing quantity.
    if (new_price == o->price && new_quantity <= o->quantity) [[likely]] {
      o->level->total_quantity -= (o->quantity - new_quantity);

      o->quantity = new_quantity;
      return {};
    }

    // SLOW PATH: Price change or quantity increase.
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
    // levels and count for opposite side
    auto& levels = (S == Side::Buy) ? ask_levels : bid_levels;
    auto& count = (S == Side::Buy) ? ask_count : bid_count;

    while (incoming_qty > 0 && count > 0) {
      PriceLevel* best_level = levels[0];
      uint64_t best_price = best_level->price;

      // Breaks if prices don't cross
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
          // Resting order is fully filled, destroy it
          uint64_t resting_id = resting->id;
          best_level->remove(resting);
          order_pool.deallocate(resting);
          order_lookup[resting_id] = nullptr;
        }
        resting = next_resting;
      }
      // If the level is completely drained of liquidity, destroy the level
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

    uint32_t remaining_qty = quantity;

    remaining_qty = match<S>(id, price, quantity);
    if (remaining_qty > 0) {
      return add_order<S>(id, price, remaining_qty);
    }

    return {};
  }

  [[nodiscard]] const PriceLevel* get_best_bid() const {
    if (bid_count == 0) [[unlikely]]
      return nullptr;
    return bid_levels[0];  // Highest buyer
  }

  [[nodiscard]] const PriceLevel* get_best_ask() const {
    if (ask_count == 0) [[unlikely]]
      return nullptr;
    return ask_levels[0];  // Lowest seller
  }

  template <Side S>
  size_t get_l2_snapshot(LevelInfo* out_buffer, size_t max_depth) const {
    const auto& levels = (S == Side::Buy) ? bid_levels : ask_levels;
    const size_t count = (S == Side::Buy) ? bid_count : ask_count;

    size_t depth = std::min(max_depth, count);

    for (size_t i = 0; i < depth; ++i) {
      out_buffer[i] = {levels[i]->price, levels[i]->total_quantity};
    }

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
