#include <gtest/gtest.h>

#include "gateway/buffer.hpp"

TEST(Buffer, WriteReadWritePositionVerification) {
  Buffer buf;
  std::array<std::byte, 100> data100{};
  std::array<std::byte, 60> data60{};
  std::fill(data100.begin(), data100.end(), std::byte{0xAB});
  std::fill(data60.begin(), data60.end(), std::byte{0xCD});

  buf.write(data100.data(), 100);
  EXPECT_EQ(buf.readable(), 100);

  buf.consume(50);
  EXPECT_EQ(buf.readable(), 50);

  buf.write(data60.data(), 60);
  EXPECT_EQ(buf.readable(), 110);

  const std::byte* ptr = buf.read_ptr();
  EXPECT_EQ(ptr[0], std::byte{0xAB});
  EXPECT_EQ(ptr[49], std::byte{0xAB});
  EXPECT_EQ(ptr[50], std::byte{0xCD});
  EXPECT_EQ(ptr[109], std::byte{0xCD});
}
