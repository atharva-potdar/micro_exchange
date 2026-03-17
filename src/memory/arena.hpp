#pragma once
#include <cstddef>
#include <cstring>
#include <utility>
#include <cassert>

template <typename T, std::size_t Capacity> class Arena {
private:
  static_assert(sizeof(T) >= sizeof(T *), "T must be at least pointer-sized.");
  static_assert(Capacity > 0, "Capacity must be strictly positive.");
  static_assert(std::is_trivially_destructible_v<T>, "Arena does not auto-destruct active elements on shutdown. T must be trivially destructible.");

  alignas(alignof(T)) std::byte storage[Capacity * sizeof(T)];
  T *free_head;
  size_t allocated;

public:
  Arena() {
    allocated = 0;
    for (std::size_t i = 0; i < Capacity; i++) {
      T *slot = reinterpret_cast<T *>(&storage[i * sizeof(T)]);
      T *next;
      if (i + 1 < Capacity) {
        next = reinterpret_cast<T *>(&storage[(i + 1) * sizeof(T)]);
      } else {
        next = nullptr;
      }
      std::memcpy(static_cast<void*>(slot), &next, sizeof(T *));
    }
    free_head = reinterpret_cast<T *>(&storage[0]);
  }

  template <typename... Args>
  [[nodiscard]] T *allocate(Args &&...args) {
    if (free_head == nullptr) {
      return nullptr;
    }
    T *result = free_head;
    std::memcpy(&free_head, static_cast<void*>(result), sizeof(T *));
    allocated++;
    return new (result) T(std::forward<Args>(args)...);
  }

  void deallocate(T *ptr) {
    if (ptr == nullptr) {
      return;
    }

    std::byte* byte_ptr = reinterpret_cast<std::byte*>(ptr);
    assert(byte_ptr >= storage && byte_ptr < storage + (Capacity * sizeof(T)) && "Pointer does not belong to this Arena");
    assert(static_cast<size_t>(byte_ptr - storage) % sizeof(T) == 0 && "Pointer is not aligned to Arena boundaries");
    assert(allocated > 0 && "Arena allocated count underflow");

    ptr->~T();
    std::memcpy(static_cast<void*>(ptr), &free_head, sizeof(T *));
    free_head = ptr;
    allocated--;
  }

  [[nodiscard]] size_t size() const { return allocated; }

  [[nodiscard]] size_t capacity() const { return Capacity; }
};
