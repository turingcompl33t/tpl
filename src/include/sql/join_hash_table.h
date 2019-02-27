#pragma once

#include "sql/bloom_filter.h"
#include "sql/concise_hash_table.h"
#include "sql/generic_hash_table.h"
#include "util/chunked_vector.h"
#include "util/region.h"

namespace tpl::sql::test {
class JoinHashTableTest;
}  // namespace tpl::sql::test

namespace tpl::sql {

class VectorProjectionIterator;

class JoinHashTable {
 public:
  /// Construct a hash-table used for join processing using \a region as the
  /// main memory allocator
  JoinHashTable(util::Region *region, u32 tuple_size,
                bool use_concise_ht = false) noexcept;

  /// This class cannot be copied or moved
  DISALLOW_COPY_AND_MOVE(JoinHashTable);

  /// Allocate storage in the hash table for an input tuple whose hash value is
  /// \a hash and whose size (in bytes) is \a tuple_size. Remember that this
  /// only performs an allocation from the table's memory pool. No insertion
  /// into the table is performed.
  byte *AllocInputTuple(hash_t hash);

  /// Fully construct the join hash table. If the join hash table has already
  /// been built, do nothing.
  void Build();

  class Iterator;

  /// Lookup a single entry with hash value \a hash returning an iterator
  Iterator Lookup(hash_t hash) const;

  /// Perform a vectorized lookup
  void LookupBatch(u32 num_tuples, const hash_t hashes[],
                   const HashTableEntry *results[]) const;

  // -------------------------------------------------------
  // Simple Accessors
  // -------------------------------------------------------

  /// Return the total number of inserted elements, including duplicates
  u32 num_elems() const { return num_elems_; }

  /// Has the hash table been built?
  bool is_built() const { return built_; }

  /// Is this join using a concise hash table?
  bool use_concise_hash_table() const { return use_concise_ht_; }

 public:
  // -------------------------------------------------------
  // Tuple-at-a-time Iterator
  // -------------------------------------------------------

  /// The iterator used for generic lookups. This class is used mostly for
  /// tuple-at-a-time lookups from the hash table.
  class Iterator {
   public:
    Iterator(HashTableEntry *initial, hash_t hash);

    using KeyEq = bool(void *opaque_ctx, void *probe_tuple, void *table_tuple);
    HashTableEntry *NextMatch(KeyEq key_eq, void *opaque_ctx,
                              void *probe_tuple);

   private:
    // The next element the iterator produces
    HashTableEntry *next_;
    // The hash value we're looking up
    hash_t hash_;
  };

 private:
  friend class tpl::sql::test::JoinHashTableTest;

  // Access a stored entry by index
  HashTableEntry *EntryAt(const u64 idx) noexcept {
    return reinterpret_cast<HashTableEntry *>(entries_[idx]);
  }

  // Dispatched from Build() to build either a generic or concise hash table
  void BuildGenericHashTable();
  void BuildConciseHashTable();

  // Dispatched from BuildConciseHashTable() to reorder elements based on
  // ordering from the concise hash table
  void InsertIntoConciseHashTable() noexcept;
  template <bool PREFETCH>
  void InsertIntoConciseHashTableInternal() noexcept;
  void ReorderMainEntries() noexcept;
  void ReorderOverflowEntries() noexcept;
  void VerifyMainEntryOrder() noexcept;
  void VerifyOverflowEntryOrder() noexcept;

  // Dispatched from LookupBatch() to lookup from either a generic or concise
  // hash table in batched manner
  void LookupBatchInGenericHashTable(u32 num_tuples, const hash_t hashes[],
                                     const HashTableEntry *results[]) const;
  void LookupBatchInConciseHashTable(u32 num_tuples, const hash_t hashes[],
                                     const HashTableEntry *results[]) const;

 private:
  // The vector where we store the build-side input
  util::ChunkedVector entries_;

  // The generic hash table
  GenericHashTable generic_hash_table_;

  // The concise hash table
  ConciseHashTable concise_hash_table_;

  // The bloom filter
  BloomFilter bloom_filter_;

  // The head of the lazy insertion list
  HashTableEntry head_;

  // The number of elements inserted
  u32 num_elems_;

  // Has the hash table been built?
  bool built_;

  // Should we use a concise hash table?
  bool use_concise_ht_;
};

// ---------------------------------------------------------
// JoinHashTable implementation
// ---------------------------------------------------------

inline byte *JoinHashTable::AllocInputTuple(hash_t hash) {
  auto *entry = reinterpret_cast<HashTableEntry *>(entries_.append());
  entry->hash = hash;
  entry->next = head_.next;
  head_.next = entry;

  num_elems_++;

  return entry->payload;
}

inline JoinHashTable::Iterator JoinHashTable::Lookup(const hash_t hash) const {
  HashTableEntry *entry = generic_hash_table_.FindChainHead(hash);
  while (entry != nullptr && entry->hash != hash) {
    entry = entry->next;
  }
  return JoinHashTable::Iterator(entry, hash);
}

// ---------------------------------------------------------
// JoinHashTable's Iterator implementation
// ---------------------------------------------------------

inline JoinHashTable::Iterator::Iterator(HashTableEntry *initial, hash_t hash)
    : next_(initial), hash_(hash) {}

inline HashTableEntry *JoinHashTable::Iterator::NextMatch(
    JoinHashTable::Iterator::KeyEq key_eq, void *opaque_ctx,
    void *probe_tuple) {
  HashTableEntry *result = next_;
  while (result != nullptr) {
    next_ = next_->next;
    if (result->hash == hash_ &&
        key_eq(opaque_ctx, probe_tuple,
               reinterpret_cast<void *>(result->payload))) {
      break;
    }
    result = next_;
  }
  return result;
}

}  // namespace tpl::sql
