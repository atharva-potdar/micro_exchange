#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

enum class MessageType : uint8_t {
  OrderAdd = 0x01,
  OrderCancel = 0x02,
  Ack = 0x03,
  Trade = 0x04
};

// little-endian native
struct WireHeader {
  uint16_t length;
  uint8_t type;
} __attribute__((packed));  // always first

static_assert(sizeof(WireHeader) == 3);

struct WireOrderAdd {
  WireHeader hdr;
  uint64_t id;
  uint64_t price;
  uint32_t qty;
  uint8_t side;

} __attribute__((packed));

inline auto serialize(const WireOrderAdd& msg, std::byte* buf) -> size_t {
  size_t offset = 0;
  std::memcpy(buf + offset, &msg.hdr, sizeof(msg.hdr));
  offset += sizeof(msg.hdr);
  std::memcpy(buf + offset, &msg.id, sizeof(msg.id));
  offset += sizeof(msg.id);
  std::memcpy(buf + offset, &msg.price, sizeof(msg.price));
  offset += sizeof(msg.price);
  std::memcpy(buf + offset, &msg.qty, sizeof(msg.qty));
  offset += sizeof(msg.qty);
  std::memcpy(buf + offset, &msg.side, sizeof(msg.side));
  offset += sizeof(msg.side);

  return offset;
}

inline auto parse_order_add(const std::byte* buf) -> WireOrderAdd {
  WireOrderAdd woa;
  size_t offset = 0;
  std::memcpy(&woa.hdr, buf + offset, sizeof(woa.hdr));
  offset += sizeof(woa.hdr);
  std::memcpy(&woa.id, buf + offset, sizeof(woa.id));
  offset += sizeof(woa.id);
  std::memcpy(&woa.price, buf + offset, sizeof(woa.price));
  offset += sizeof(woa.price);
  std::memcpy(&woa.qty, buf + offset, sizeof(woa.qty));
  offset += sizeof(woa.qty);
  std::memcpy(&woa.side, buf + offset, sizeof(woa.side));
  offset += sizeof(woa.side);

  return woa;
}

static_assert(sizeof(WireOrderAdd) == 24);

struct WireOrderCancel {
  WireHeader hdr;
  uint64_t id;
} __attribute__((packed));

inline auto serialize(const WireOrderCancel& msg, std::byte* buf) -> size_t {
  size_t offset = 0;
  std::memcpy(buf + offset, &msg.hdr, sizeof(msg.hdr));
  offset += sizeof(msg.hdr);
  std::memcpy(buf + offset, &msg.id, sizeof(msg.id));
  offset += sizeof(msg.id);

  return offset;
}

inline auto parse_order_cancel(const std::byte* buf) -> WireOrderCancel {
  WireOrderCancel woc;
  size_t offset = 0;
  std::memcpy(&woc.hdr, buf + offset, sizeof(woc.hdr));
  offset += sizeof(woc.hdr);
  std::memcpy(&woc.id, buf + offset, sizeof(woc.id));
  offset += sizeof(woc.id);

  return woc;
}

static_assert(sizeof(WireOrderCancel) == 11);

struct WireAck {
  WireHeader hdr;
  uint64_t id;
  uint8_t status;
} __attribute__((packed));

inline auto serialize(const WireAck& msg, std::byte* buf) -> size_t {
  size_t offset = 0;
  std::memcpy(buf + offset, &msg.hdr, sizeof(msg.hdr));
  offset += sizeof(msg.hdr);
  std::memcpy(buf + offset, &msg.id, sizeof(msg.id));
  offset += sizeof(msg.id);
  std::memcpy(buf + offset, &msg.status, sizeof(msg.status));
  offset += sizeof(msg.status);

  return offset;
}

inline auto parse_ack(const std::byte* buf) -> WireAck {
  WireAck wa;
  size_t offset = 0;
  std::memcpy(&wa.hdr, buf + offset, sizeof(wa.hdr));
  offset += sizeof(wa.hdr);
  std::memcpy(&wa.id, buf + offset, sizeof(wa.id));
  offset += sizeof(wa.id);
  std::memcpy(&wa.status, buf + offset, sizeof(wa.status));
  offset += sizeof(wa.status);

  return wa;
}

static_assert(sizeof(WireAck) == 12);

struct WireTrade {
  WireHeader hdr;
  uint64_t buyer;
  uint64_t seller;
  uint64_t price;
  uint32_t qty;
} __attribute__((packed));

inline auto serialize(const WireTrade& msg, std::byte* buf) -> size_t {
  size_t offset = 0;
  std::memcpy(buf + offset, &msg.hdr, sizeof(msg.hdr));
  offset += sizeof(msg.hdr);
  std::memcpy(buf + offset, &msg.buyer, sizeof(msg.buyer));
  offset += sizeof(msg.buyer);
  std::memcpy(buf + offset, &msg.seller, sizeof(msg.seller));
  offset += sizeof(msg.seller);
  std::memcpy(buf + offset, &msg.price, sizeof(msg.price));
  offset += sizeof(msg.price);
  std::memcpy(buf + offset, &msg.qty, sizeof(msg.qty));
  offset += sizeof(msg.qty);
  return offset;
}

inline auto parse_trade(const std::byte* buf) -> WireTrade {
  WireTrade wt;
  size_t offset = 0;
  std::memcpy(&wt.hdr, buf + offset, sizeof(wt.hdr));
  offset += sizeof(wt.hdr);
  std::memcpy(&wt.buyer, buf + offset, sizeof(wt.buyer));
  offset += sizeof(wt.buyer);
  std::memcpy(&wt.seller, buf + offset, sizeof(wt.seller));
  offset += sizeof(wt.seller);
  std::memcpy(&wt.price, buf + offset, sizeof(wt.price));
  offset += sizeof(wt.price);
  std::memcpy(&wt.qty, buf + offset, sizeof(wt.qty));
  offset += sizeof(wt.qty);
  return wt;
}

static_assert(sizeof(WireTrade) == 31);
