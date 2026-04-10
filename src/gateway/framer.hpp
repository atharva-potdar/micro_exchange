#pragma once

#include <cstddef>

#include "buffer.hpp"
#include "protocol.hpp"

class Framer {
 private:
  Buffer buffer;

 public:
  void ingest(const std::byte* data, size_t len) { buffer.write(data, len); }

  template <typename Handler>
  bool try_parse_one(Handler& handler) {
    if (buffer.readable() < sizeof(WireHeader)) [[unlikely]] {
      return false;
    }

    const auto* hdr = reinterpret_cast<const WireHeader*>(buffer.read_ptr());

    if (buffer.readable() < hdr->length) [[unlikely]] {
      return false;
    }

    const auto type = static_cast<MessageType>(hdr->type);

    if (type == MessageType::OrderAdd) [[likely]] {
      handler(*reinterpret_cast<const WireOrderAdd*>(buffer.read_ptr()));
    } else if (type == MessageType::OrderCancel) {
      handler(*reinterpret_cast<const WireOrderCancel*>(buffer.read_ptr()));
    }

    buffer.consume(hdr->length);
    return true;
  }
};
