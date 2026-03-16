#pragma once
#include <cstdint>

enum class Side : uint8_t {
  Buy,
  Sell
};

enum class Error : uint8_t {
  OrderNotFound,
  DuplicateOrderId,
  InvalidQuantity,
  InvalidPrice,
  PoolExhausted
};

struct Order {
  uint64_t id = 0;
  uint64_t price = 0;
  uint32_t quantity = 0;
  Side side = Side::Buy;
  Order* prev = nullptr;
  Order* next = nullptr;
};

struct Trade {
  uint64_t buyer_order_id = 0;
  uint64_t seller_order_id = 0;
  uint64_t price = 0;
  uint32_t quantity = 0;
};
