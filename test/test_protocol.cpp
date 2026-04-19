#include <gtest/gtest.h>

#include "gateway/protocol.hpp"
#include "orderbook/types.hpp"

TEST(Protocol, WireOrderAddRoundTrip) {
  WireOrderAdd og{};
  og.hdr = {.length = sizeof(WireOrderAdd),
            .type = static_cast<uint8_t>(MessageType::OrderAdd)};
  og.id = 42;
  og.price = 100500;
  og.qty = 25;
  og.side = static_cast<uint8_t>(Side::Buy);

  std::byte buf[sizeof(WireOrderAdd)];
  serialize(og, buf);
  WireOrderAdd woa = parse_order_add(buf);

  EXPECT_EQ(woa.id, og.id);
  EXPECT_EQ(woa.qty, og.qty);
  EXPECT_EQ(woa.price, og.price);
  EXPECT_EQ(woa.side, og.side);
  EXPECT_EQ(woa.hdr.length, og.hdr.length);
  EXPECT_EQ(woa.hdr.type, og.hdr.type);
}

TEST(Protocol, WireOrderCancelRoundTrip) {
  WireOrderCancel og{};
  og.hdr = {.length = sizeof(WireOrderCancel),
            .type = static_cast<uint8_t>(MessageType::OrderCancel)};
  og.id = 99;
  std::byte buf[sizeof(WireOrderCancel)];
  serialize(og, buf);
  WireOrderCancel woc = parse_order_cancel(buf);
  EXPECT_EQ(woc.id, og.id);
  EXPECT_EQ(woc.hdr.length, og.hdr.length);
  EXPECT_EQ(woc.hdr.type, og.hdr.type);
}

TEST(Protocol, WireAckRoundTrip) {
  WireAck og{};
  og.hdr = {.length = sizeof(WireAck),
            .type = static_cast<uint8_t>(MessageType::Ack)};
  og.id = 42;
  og.status = 0x00;
  std::byte buf[sizeof(WireAck)];
  serialize(og, buf);
  WireAck wa = parse_ack(buf);
  EXPECT_EQ(wa.id, og.id);
  EXPECT_EQ(wa.status, og.status);
  EXPECT_EQ(wa.hdr.length, og.hdr.length);
  EXPECT_EQ(wa.hdr.type, og.hdr.type);
}

TEST(Protocol, WireTradeRoundTrip) {
  WireTrade og{};
  og.hdr = {.length = sizeof(WireTrade),
            .type = static_cast<uint8_t>(MessageType::Trade)};
  og.buyer = 42;
  og.seller = 99;
  og.price = 100500;
  og.qty = 25;
  std::byte buf[sizeof(WireTrade)];
  serialize(og, buf);
  WireTrade wt = parse_trade(buf);
  EXPECT_EQ(wt.buyer, og.buyer);
  EXPECT_EQ(wt.seller, og.seller);
  EXPECT_EQ(wt.price, og.price);
  EXPECT_EQ(wt.qty, og.qty);
  EXPECT_EQ(wt.hdr.length, og.hdr.length);
  EXPECT_EQ(wt.hdr.type, og.hdr.type);
}
