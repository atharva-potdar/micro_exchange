#pragma once
#include <cstdint>
#include <cassert>
#include "orderbook/types.hpp"

struct PriceLevel {
  uint64_t price = 0;
  Order* head = nullptr;
  Order* tail = nullptr;
  uint32_t order_count = 0;
  uint64_t total_quantity = 0;

  [[nodiscard]] bool empty() const {
    return head == nullptr;
  }

  [[nodiscard]] Order* front() const {
    return head;
  }

  void push_back(Order* o) {
    assert(o != nullptr && "Cannot push a null order");
    assert(o->price == this->price && "Order price does not match PriceLevel price");
    assert(o->quantity > 0 && "Cannot push an order with 0 quantity");

    o->next = nullptr;
    o->prev = tail;
    if (tail) {
      tail->next = o;
    } else {
      head = o;
    }
    tail = o;
    order_count++;
    total_quantity += o->quantity;
  }

  void remove(Order* o) {
    assert(o != nullptr && "Cannot remove a null order");
    assert(order_count > 0 && "Cannot remove from an empty price level");
    assert(o->price == this->price && "Order does not belong to this PriceLevel");
    assert((order_count > 1 || total_quantity == o->quantity) && "Quantity mismatch on last order");

    if (o->prev) {
      o->prev->next = o->next;
    } else {
      head = o->next;
    }
    if (o->next) {
      o->next->prev = o->prev;
    } else {
      tail = o->prev;
    }
    o->prev = nullptr;
    o->next = nullptr;
    order_count--;
    total_quantity -= o->quantity;

    assert((head != nullptr || tail == nullptr) && "List corrupted: head is null but tail is not");
    assert((tail != nullptr || head == nullptr) && "List corrupted: tail is null but head is not");
    assert((order_count > 0 || total_quantity == 0) && "List corrupted: 0 orders but quantity > 0");
  }

  void update_quantity(Order *o, uint32_t new_quantity) {
    assert(o != nullptr && "Cannot remove a null order");
    assert(o->price == this->price && "Order does not belong to this PriceLevel");
    assert(new_quantity > o->quantity && "Cannot increase order quantity");

    if (new_quantity == 0) {
      remove(o);
    } else {
      total_quantity -= (o->quantity - new_quantity);
    }
  }
};
