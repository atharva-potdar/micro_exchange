#include <gtest/gtest.h>

#include "gateway/framer.hpp"
#include "gateway/protocol.hpp"

struct TestHandler {
  size_t add_count = 0;
  size_t cancel_count = 0;
  uint64_t last_id = 0;

  void operator()(const WireOrderAdd& msg) {
    add_count++;
    last_id = msg.id;
  }

  void operator()(const WireOrderCancel& msg) {
    cancel_count++;
    last_id = msg.id;
  }
};

TEST(Framer, FeedOneByteAtATime) {
  Framer framer;
  TestHandler handler;

  WireOrderAdd og{};
  og.hdr = {.length=sizeof(WireOrderAdd), .type=static_cast<uint8_t>(MessageType::OrderAdd)};
  og.id = 42;
  og.price = 100500;
  og.qty = 25;
  og.side = 1;

  std::byte buf[sizeof(WireOrderAdd)];
  serialize(og, buf);

  for (size_t i = 0; i < sizeof(WireOrderAdd); ++i) {
    framer.ingest(buf + i, 1);
    bool parsed = framer.try_parse_one(handler);

    if (i < sizeof(WireOrderAdd) - 1) {
      EXPECT_FALSE(parsed);
      EXPECT_EQ(handler.add_count, 0);
    } else {
      EXPECT_TRUE(parsed);
      EXPECT_EQ(handler.add_count, 1);
      EXPECT_EQ(handler.last_id, og.id);
    }
  }
}

TEST(Framer, ParseConcatenatedMessages) {
  Framer framer;
  TestHandler handler;

  WireOrderAdd m1{};
  m1.hdr = {.length=sizeof(WireOrderAdd), .type=static_cast<uint8_t>(MessageType::OrderAdd)};
  m1.id = 1;
  m1.price = 100;
  m1.qty = 10;
  m1.side = 1;

  WireOrderCancel m2{};
  m2.hdr = {.length=sizeof(WireOrderCancel),
            .type=static_cast<uint8_t>(MessageType::OrderCancel)};
  m2.id = 2;

  WireOrderAdd m3{};
  m3.hdr = {.length=sizeof(WireOrderAdd), .type=static_cast<uint8_t>(MessageType::OrderAdd)};
  m3.id = 3;
  m3.price = 200;
  m3.qty = 20;
  m3.side = 1;

  std::byte buf[sizeof(WireOrderAdd) * 2 + sizeof(WireOrderCancel)];
  size_t offset = 0;
  offset += serialize(m1, buf + offset);
  offset += serialize(m2, buf + offset);
  offset += serialize(m3, buf + offset);

  framer.ingest(buf, offset);

  EXPECT_TRUE(framer.try_parse_one(handler));
  EXPECT_EQ(handler.add_count, 1);
  EXPECT_EQ(handler.cancel_count, 0);
  EXPECT_EQ(handler.last_id, m1.id);

  EXPECT_TRUE(framer.try_parse_one(handler));
  EXPECT_EQ(handler.add_count, 1);
  EXPECT_EQ(handler.cancel_count, 1);
  EXPECT_EQ(handler.last_id, m2.id);

  EXPECT_TRUE(framer.try_parse_one(handler));
  EXPECT_EQ(handler.add_count, 2);
  EXPECT_EQ(handler.cancel_count, 1);
  EXPECT_EQ(handler.last_id, m3.id);

  EXPECT_FALSE(framer.try_parse_one(handler));
}
