#pragma once
#include <array>
#include <cstddef>
#include <cstring>

class Buffer {
 private:
  std::array<std::byte, 65536> data{};
  size_t read_pos = 0;
  size_t write_pos = 0;

 public:
  void write(const std::byte* src, size_t len) {
    std::memcpy(data.data() + write_pos, src, len);
    write_pos += len;
  }

  [[nodiscard]] auto readable() const -> size_t { return write_pos - read_pos; }

  [[nodiscard]] auto read_ptr() const -> const std::byte* { return data.data() + read_pos; }

  void consume(size_t len) {
    read_pos += len;
    if (read_pos >= write_pos) {
      read_pos = 0;
      write_pos = 0;
    } else if (write_pos > 65536 - 256) {
      size_t unread = readable();
      std::memmove(data.data(), data.data() + read_pos, unread);
      read_pos = 0;
      write_pos = unread;
    }
  }
};
