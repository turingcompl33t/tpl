#include <algorithm>
#include <random>

#include "sql/runtime_types.h"
#include "util/memory.h"
#include "util/test_harness.h"

namespace tpl::sql {

class VarlenEntryTest : public TplTest {};

TEST_F(VarlenEntryTest, Basic) {
  std::default_random_engine gen;
  std::uniform_int_distribution<uint8_t> dist(0, std::numeric_limits<uint8_t>::max());

  const uint32_t large_size = 40;
  byte *large_buf = static_cast<byte *>(util::MallocAligned(large_size, 8));
  std::generate(large_buf, large_buf + large_size, [&]() { return static_cast<byte>(dist(gen)); });

  VarlenEntry entry = VarlenEntry::Create(large_buf, large_size);
  EXPECT_FALSE(entry.IsInlined());
  EXPECT_EQ(0u, std::memcmp(entry.GetPrefix(), large_buf, VarlenEntry::GetPrefixSize()));
  EXPECT_EQ(large_buf, entry.GetContent());

  std::free(large_buf);

  const uint32_t small_size = 10;
  byte small_buf[small_size];
  std::generate(small_buf, small_buf + small_size, [&]() { return static_cast<byte>(dist(gen)); });

  entry = VarlenEntry::Create(small_buf, small_size);
  EXPECT_TRUE(entry.IsInlined());
  EXPECT_EQ(0u, std::memcmp(entry.GetPrefix(), small_buf, VarlenEntry::GetPrefixSize()));
  EXPECT_EQ(0u, std::memcmp(entry.GetContent(), small_buf, small_size));
  EXPECT_NE(small_buf, entry.GetContent());
}

TEST_F(VarlenEntryTest, Comparison) {
  // Small/Small
  {
    auto e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("helo"), 4);
    auto e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("fuck"), 4);
    EXPECT_NE(e1, e2);
    EXPECT_GT(e1, e2);

    e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("he"), 2);
    e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hell"), 4);
    EXPECT_NE(e1, e2);

    e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("hi"), 2);
    e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hi"), 2);
    EXPECT_EQ(e1, e2);
  }

  // Small/Medium
  {
    auto e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("helo"), 4);
    auto e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("fuckyou"), 7);
    EXPECT_NE(e1, e2);

    e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("he"), 2);
    e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hellothere"), 10);
    EXPECT_NE(e1, e2);

    e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("hi"), 2);
    e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hi"), 2);
    EXPECT_EQ(e1, e2);
  }

  // Medium/Medium
  {
    auto e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("hello"), 5);
    auto e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hellothere"), 10);
    EXPECT_NE(e1, e2);
    EXPECT_LT(e1, e2);

    e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("hello"), 5);
    e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hiyathere"), 9);
    EXPECT_NE(e1, e2);
    EXPECT_LT(e1, e2);
  }

  // Large/Large
  {
    auto e1 = VarlenEntry::Create(reinterpret_cast<const byte*>("nottodayson"), 11);
    auto e2 = VarlenEntry::Create(reinterpret_cast<const byte*>("hellotherebro"), 13);
    EXPECT_NE(e1, e2);
    EXPECT_GT(e1, e2);
  }
}

}  // namespace tpl::sql
