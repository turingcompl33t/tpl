#include "tpl_test.h"

#include <deque>
#include <random>

#include "util/chunked_vector.h"

namespace tpl::util::test {

class GenericChunkedVectorTest : public TplTest {};

TEST_F(GenericChunkedVectorTest, InsertAndIndexTest) {
  const u32 num_elems = 10;

  util::Region tmp("tmp");
  ChunkedVectorT<u32> vec(&tmp);

  EXPECT_TRUE(vec.empty());

  for (u32 i = 0; i < num_elems; i++) {
    vec.push_back(i);
  }

  EXPECT_FALSE(vec.empty());
  EXPECT_EQ(num_elems, vec.size());
}

TEST_F(GenericChunkedVectorTest, RandomLookupTest) {
  const u32 num_elems = 1000;

  util::Region tmp("tmp");
  ChunkedVectorT<u32> vec(&tmp);

  EXPECT_TRUE(vec.empty());

  for (u32 i = 0; i < num_elems; i++) {
    vec.push_back(i);
  }

  // Do a bunch of random lookup
  std::random_device random;
  for (u32 i = 0; i < 1000; i++) {
    auto idx = random() % num_elems;
    EXPECT_EQ(idx, vec[idx]);
  }
}

TEST_F(GenericChunkedVectorTest, IterationTest) {
  util::Region tmp("tmp");
  ChunkedVectorT<u32> vec(&tmp);

  for (u32 i = 0; i < 10; i++) {
    vec.push_back(i);
  }

  {
    u32 i = 0;
    for (auto x : vec) {
      EXPECT_EQ(i++, x);
    }
  }
}

TEST_F(GenericChunkedVectorTest, PopBackTest) {
  util::Region tmp("tmp");
  ChunkedVectorT<u32> vec(&tmp);

  for (u32 i = 0; i < 10; i++) {
    vec.push_back(i);
  }

  vec.pop_back();
  EXPECT_EQ(9u, vec.size());

  vec.pop_back();
  EXPECT_EQ(8u, vec.size());

  for (u32 i = 0; i < vec.size(); i++) {
    EXPECT_EQ(i, vec[i]);
  }
}

TEST_F(GenericChunkedVectorTest, FrontBackTest) {
  util::Region tmp("tmp");
  ChunkedVectorT<u32> vec(&tmp);

  for (u32 i = 0; i < 10; i++) {
    vec.push_back(i);
  }

  EXPECT_EQ(0u, vec.front());
  EXPECT_EQ(9u, vec.back());

  vec.front() = 44;
  vec.back() = 100;

  EXPECT_EQ(44u, vec[0]);
  EXPECT_EQ(100u, vec[9]);

  vec.pop_back();
  EXPECT_EQ(8u, vec.back());
}

TEST_F(GenericChunkedVectorTest, DISABLED_PerfInsertTest) {
  auto stdvec_ms = Bench(3, []() {
    std::vector<u32> v;
    for (u32 i = 0; i < 10000000; i++) {
      v.push_back(i);
    }
  });

  auto stddeque_ms = Bench(3, []() {
    std::deque<u32> v;
    for (u32 i = 0; i < 10000000; i++) {
      v.push_back(i);
    }
  });

  auto chunked_ms = Bench(3, []() {
    util::Region tmp("tmp");
    ChunkedVectorT<u32> v(&tmp);
    for (u32 i = 0; i < 10000000; i++) {
      v.push_back(i);
    }
  });

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "std::vector  : " << stdvec_ms << " ms" << std::endl;
  std::cout << "std::deque   : " << stddeque_ms << " ms" << std::endl;
  std::cout << "ChunkedVector: " << chunked_ms << " ms" << std::endl;
}

TEST_F(GenericChunkedVectorTest, DISABLED_PerfScanTest) {
  static const u32 num_elems = 10000000;

  std::vector<u32> stdvec;
  std::deque<u32> stddeque;
  util::Region tmp("tmp");
  ChunkedVectorT<u32> chunkedvec(&tmp);
  for (u32 i = 0; i < num_elems; i++) {
    stdvec.push_back(i);
    stddeque.push_back(i);
    chunkedvec.push_back(i);
  }

  auto stdvec_ms = Bench(10, [&stdvec]() {
    auto c = 0;
    for (auto x : stdvec) {
      c += x;
    }
    stdvec[0] = c;
  });

  auto stddeque_ms = Bench(10, [&stddeque]() {
    auto c = 0;
    for (auto x : stddeque) {
      c += x;
    }
    stddeque[0] = c;
  });

  auto chunked_ms = Bench(10, [&chunkedvec]() {
    u32 c = 0;
    for (auto x : chunkedvec) {
      c += x;
    }
    chunkedvec[0] = c;
  });

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "std::vector  : " << stdvec_ms << " ms" << std::endl;
  std::cout << "std::deque   : " << stddeque_ms << " ms" << std::endl;
  std::cout << "ChunkedVector: " << chunked_ms << " ms" << std::endl;
}

TEST_F(GenericChunkedVectorTest, DISABLED_PerfRandomAccessTest) {
  static const u32 num_elems = 10000000;

  std::vector<u32> stdvec;
  std::deque<u32> stddeque;
  util::Region tmp("tmp");
  ChunkedVectorT<u32> chunkedvec(&tmp);
  for (u32 i = 0; i < num_elems; i++) {
    stdvec.push_back(i % 4);
    stddeque.push_back(i % 4);
    chunkedvec.push_back(i % 4);
  }

  std::vector<u32> random_indexes(num_elems);
  for (u32 i = 0; i < num_elems; i++) {
    random_indexes[i] = (rand() % num_elems);
  }

  auto stdvec_ms = Bench(10, [&stdvec, &random_indexes]() {
    auto c = 0;
    for (auto idx : random_indexes) {
      c += stdvec[idx];
    }
    stdvec[0] = c;
  });

  auto stddeque_ms = Bench(10, [&stddeque, &random_indexes]() {
    auto c = 0;
    for (auto idx : random_indexes) {
      c += stddeque[idx];
    }
    stddeque[0] = c;
  });

  auto chunked_ms = Bench(10, [&chunkedvec, &random_indexes]() {
    u32 c = 0;
    for (auto idx : random_indexes) {
      c += chunkedvec[idx];
    }
    chunkedvec[0] = c;
  });

  std::cout << std::fixed << std::setprecision(4);
  std::cout << "std::vector  : " << stdvec_ms << " ms" << std::endl;
  std::cout << "std::deque   : " << stddeque_ms << " ms" << std::endl;
  std::cout << "ChunkedVector: " << chunked_ms << " ms" << std::endl;
}

}  // namespace tpl::util::test
