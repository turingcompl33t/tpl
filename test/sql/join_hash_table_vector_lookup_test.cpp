#include "tpl_test.h"

#include <random>

#include "sql/join_hash_table.h"
#include "sql/join_hash_table_vector_lookup.h"
#include "sql/vector_projection.h"
#include "sql/vector_projection_iterator.h"
#include "util/hash.h"

namespace tpl::sql::test {

/// This is the tuple we insert into the hash table
template <u8 N>
struct Tuple {
  u32 build_key;
  u32 aux[N];
};

template <u8 N>
static inline hash_t HashTupleInVPI(VectorProjectionIterator *vpi) noexcept {
  const u32 *key_ptr = vpi->Get<u32, false>(0, nullptr);
  return util::Hasher::Hash((const u8 *)key_ptr, sizeof(Tuple<N>::build_key));
}

/// The function to determine whether two tuples have equivalent keys
template <u8 N>
static inline bool CmpTupleInVPI(const byte *table_tuple,
                                 VectorProjectionIterator *vpi) noexcept {
  auto lhs_key = reinterpret_cast<const Tuple<N> *>(table_tuple)->build_key;
  auto rhs_key = *vpi->Get<u32, false>(0, nullptr);
  return lhs_key == rhs_key;
}

class JoinHashTableVectorLookupTest : public TplTest {
 public:
  JoinHashTableVectorLookupTest() : region_(GetTestName()) {}

  util::Region *region() { return &region_; }

 private:
  util::Region region_;
};

template <u8 N, typename F>
std::unique_ptr<const JoinHashTable> InsertAndBuild(util::Region *region,
                                                    bool concise,
                                                    u32 num_tuples,
                                                    F &&key_gen) {
  auto jht = std::make_unique<JoinHashTable>(region, sizeof(Tuple<N>), concise);

  // Insert
  for (u32 i = 0; i < num_tuples; i++) {
    auto key = key_gen();
    auto hash = util::Hasher::Hash((const u8 *)&key, sizeof(key));
    auto *tuple = reinterpret_cast<Tuple<N> *>(jht->AllocInputTuple(hash));
    tuple->build_key = key;
  }

  // Build
  jht->Build();

  // Finish
  return jht;
}

// Sequential number functor
struct Seq {
  u32 c;
  explicit Seq(u32 cc) : c(cc) {}
  u32 operator()() noexcept { return c++; }
};

struct Range {
  std::random_device random;
  std::uniform_int_distribution<u32> dist;
  Range(u32 min, u32 max) : dist(min, max) {}
  u32 operator()() noexcept { return dist(random); }
};

// Random number functor
struct Rand {
  std::random_device random;
  Rand() = default;
  u32 operator()() noexcept { return random(); }
};

TEST_F(JoinHashTableVectorLookupTest, SimpleGenericLookupTest) {
  constexpr const u8 N = 1;
  constexpr const u32 num_build = 1000;
  constexpr const u32 num_probe = num_build * 10;

  // Create test JHT
  auto jht = InsertAndBuild<N>(region(), /*concise*/ false, num_build, Seq(0));

  // Create test probe input
  auto probe_keys = std::vector<u32>(num_probe);
  std::generate(probe_keys.begin(), probe_keys.end(), Range(0, num_build - 1));

  VectorProjection vp(2, num_probe);
  VectorProjectionIterator vpi(&vp);

  // Lookup
  JoinHashTableVectorLookup lookup(*jht);

  // Loop over all matches
  u32 count = 0;
  for (u32 i = 0; i < num_probe; i += kDefaultVectorSize) {
    u32 size = std::min(kDefaultVectorSize, num_probe - i);

    // Setup VP
    vp.ResetFromRaw((byte *)&probe_keys[i], nullptr, 0, size);
    vpi.SetVectorProjection(&vp);

    // Lookup
    lookup.Prepare(&vpi, HashTupleInVPI<N>);

    // Iterate all
    while (const auto *entry = lookup.GetNextOutput(&vpi, CmpTupleInVPI<N>)) {
      count++;
      auto ht_key = entry->PayloadAs<Tuple<N>>()->build_key;
      auto probe_key = *vpi.Get<u32, false>(0, nullptr);
      EXPECT_EQ(ht_key, probe_key);
    }
  }

  EXPECT_EQ(num_probe, count);
}

TEST_F(JoinHashTableVectorLookupTest, DISABLED_PerfLookupTest) {
  auto bench = [this](bool concise) {
    constexpr const u8 N = 1;
    constexpr const u32 num_build = 5000000;
    constexpr const u32 num_probe = num_build * 10;

    // Create test JHT
    auto jht = InsertAndBuild<N>(region(), concise, num_build, Seq(0));

    // Create test probe input
    auto probe_keys = std::vector<u32>(num_probe);
    std::generate(probe_keys.begin(), probe_keys.end(),
                  Range(0, num_build - 1));

    VectorProjection vp(2, kDefaultVectorSize);
    VectorProjectionIterator vpi(&vp);

    // Lookup
    JoinHashTableVectorLookup lookup(*jht);

    util::Timer<std::milli> timer;
    timer.Start();

    // Loop over all matches
    u32 count = 0;
    for (u32 i = 0; i < num_probe; i += kDefaultVectorSize) {
      u32 size = std::min(kDefaultVectorSize, num_probe - i);

      // Setup VP
      vp.ResetFromRaw((byte *)&probe_keys[i], nullptr, 0, size);
      vpi.SetVectorProjection(&vp);

      // Lookup
      lookup.Prepare(&vpi, HashTupleInVPI<N>);

      // Iterate all
      while (const auto *entry = lookup.GetNextOutput(&vpi, CmpTupleInVPI<N>)) {
        (void)entry;
        count++;
      }
    }

    timer.Stop();
    auto mtps = (num_probe / timer.elapsed()) / 1000.0;
    LOG_INFO("========== {} ==========", concise ? "Concise" : "Generic");
    LOG_INFO("# Probes    : {}", num_probe)
    LOG_INFO("Probe Time  : {} ms ({:.2f} Mtps)", timer.elapsed(), mtps);
  };

  bench(false);
  bench(true);
}

}  // namespace tpl::sql::test