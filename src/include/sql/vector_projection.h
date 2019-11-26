#pragma once

#include <iosfwd>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "common/common.h"
#include "common/macros.h"
#include "sql/schema.h"
#include "sql/tuple_id_list.h"
#include "sql/vector.h"

namespace tpl::sql {

class ColumnVectorIterator;

/**
 * A container representing a collection of tuples whose attributes are stored in columnar format.
 * It's used in the execution engine to represent subsets of materialized state such partitions of
 * base tables and intermediate state including hash tables or sorter instances.
 *
 * Columns in the projection have a well-defined order and are accessed using this unchanging order.
 * All columns in the projection have the same size and selection count at any given time.
 *
 * In addition to holding all vector data, vector projections also contain a tuple ID (TID) list
 * containing the IDs of all tuples that are externally visible. Child column vectors hold
 * references to the TID list owned by this projection. At any given time, projections have a
 * selected count (see VectorProjection::GetSelectedTupleCount()) that is <= the total tuple count
 * (see VectorProjection::GetTotalTupleCount()). Users can manually set the selections by calling
 * VectorProjection::SetSelections() providing a tuple ID list. The provided list must have the same
 * shape (i.e., capacity) as the projection.
 *
 * VectorProjections come in two flavors: referencing and owning projections. A referencing vector
 * projection contains a set of column vectors that reference data stored externally. An owning
 * vector projection allocates and owns a chunk of data that it partitions and assigns to all child
 * vectors. By default, it will allocate enough data for each child to have a capacity determined by
 * the global constant ::tpl::kDefaultVectorSize, usually 2048 elements. After construction, and
 * owning vector projection has a <b>zero</b> size (though it's capacity is
 * ::tpl::kDefaultVectorSize). Thus, users must explicitly set the size through
 * VectorProjection::Resize() before interacting with the projection. Resizing sets up all contained
 * column vectors.
 *
 * To create a referencing vector, use VectorProjection::InitializeEmpty(), and fill each column
 * vector with VectorProjection::ResetColumn():
 * @code
 * VectorProjection vp;
 * vp.InitializeEmpty(...);
 * vp.ResetColumn(0, ...);
 * vp.ResetColumn(1, ...);
 * ...
 * @endcode
 *
 * To create an owning vector, use VectorProjection::Initialize():
 * @code
 * VectorProjection vp;
 * vp.Initialize(...);
 * vp.Resize(10);
 * VectorOps::Fill(vp.GetColumn(0), ...);
 * @endcode
 *
 * To remove any selection vector, call VectorProjection::Reset(). This returns the projection to
 * the a state as immediately following initialization.
 *
 * @code
 * VectorProjection vp;
 * vp.Initialize(...);
 * vp.Resize(10);
 * // vp size and selected is 10
 * vp.Reset();
 * // vp size and selected are 0
 * vp.Resize(20);
 * // vp size and selected is 20
 * @endcode
 */
class VectorProjection {
  friend class VectorProjectionIterator;

 public:
  /**
   * Create an empty and uninitialized vector projection. Users must call @em Initialize() or
   * @em InitializeEmpty() to appropriately initialize the projection with the correct columns.
   *
   * @see Initialize()
   * @see InitializeEmpty()
   */
  VectorProjection();

  /**
   * This class cannot be copied or moved.
   */
  DISALLOW_COPY_AND_MOVE(VectorProjection);

  /**
   * Initialize a vector projection with column vectors of the provided types. This will create one
   * vector for each type provided in the column metadata list @em column_info. All vectors will
   * will be initialized with a maximum capacity of kDefaultVectorSize (e.g., 2048), are empty, and
   * will reference data owned by this vector projection.
   * @param col_types Metadata for columns in the projection.
   */
  void Initialize(const std::vector<TypeId> &col_types);

  /**
   * Initialize an empty vector projection with columns of the provided types. This will create an
   * empty vector of the specified type for each type provided in the column metadata list
   * @em column_info. Column vectors may only reference external data set and refreshed through
   * @em ResetColumn().
   *
   * @see VectorProjection::ResetColumn()
   *
   * @param col_types Metadata for columns in the projection.
   */
  void InitializeEmpty(const std::vector<TypeId> &col_types);

  /**
   * @return True if the projection has no tuples; false otherwise.
   */
  bool IsEmpty() const { return GetSelectedTupleCount() == 0; }

  /**
   * @return True if the projection is filtered; false otherwise.
   */
  bool IsFiltered() const { return filter_ != nullptr; }

  /**
   * @return The list of active TIDs in the projection; NULL if no tuples have been filtered out.
   */
  const TupleIdList *GetFilteredTupleIdList() { return filter_; }

  /**
   * Filter elements from the projection based on the tuple IDs in the input list @em tid_list.
   * @param tid_list The input TID list of valid tuples.
   */
  void SetFilteredSelections(const TupleIdList &tid_list);

  /**
   * Copy the full list of active TIDs in this projection into the provided TID list.
   * @param[out] tid_list The list where active TIDs in this projection are written to.
   */
  void CopySelections(TupleIdList *tid_list) const;

  /**
   * @return The metadata for the column at index @em col_idx in the projection.
   */
  TypeId GetColumnType(const uint32_t col_idx) const {
    TPL_ASSERT(col_idx < GetColumnCount(), "Out-of-bounds column access");
    return GetColumn(col_idx)->GetTypeId();
  }

  /**
   * @return The column vector at index @em col_idx as it appears in the projection.
   */
  const Vector *GetColumn(const uint32_t col_idx) const {
    TPL_ASSERT(col_idx < GetColumnCount(), "Out-of-bounds column access");
    return columns_[col_idx].get();
  }

  /**
   * @return The column vector at index @em col_idx as it appears in the projection.
   */
  Vector *GetColumn(const uint32_t col_idx) {
    TPL_ASSERT(col_idx < GetColumnCount(), "Out-of-bounds column access");
    return columns_[col_idx].get();
  }

  /**
   * Reset this vector projection to the state after initialization. This will set the counts to 0,
   * and reset each child vector to point to data owned by this projection (if it owns any).
   */
  void Reset(uint64_t num_tuples);

  /**
   * Packing (or compressing) a projection rearranges contained vector data by contiguously storing
   * only active vector elements, removing any filtered TID list.
   */
  void Pack();

  /**
   * @return The number of columns in the projection.
   */
  uint32_t GetColumnCount() const { return columns_.size(); }

  /**
   * @return The number of active, i.e., externally visible, tuples in this projection. The selected
   *         tuple count is always <= the total tuple count.
   */
  uint64_t GetSelectedTupleCount() const { return columns_.empty() ? 0 : columns_[0]->GetCount(); }

  /**
   * @return The total number of tuples in the projection, including those that may have been
   *         filtered out by a selection vector, if one exists. The total tuple count is >= the
   *         selected tuple count.
   */
  uint64_t GetTotalTupleCount() const { return columns_.empty() ? 0 : columns_[0]->GetSize(); }

  /**
   * @return The maximum capacity of this projection. In other words, the maximum number of tuples
   *         the vectors constituting this projection can store.
   */
  uint64_t GetTupleCapacity() const { return columns_.empty() ? 0 : columns_[0]->GetCapacity(); }

  /**
   * @return The selectivity of this projection, i.e., the fraction of **total** tuples that have
   *         passed any filters (through the selection vector), and are externally visible. The
   *         selectivity is a floating-point number in the range [0.0, 1.0].
   */
  double ComputeSelectivity() const { return IsEmpty() ? 0 : owned_tid_list_.ComputeSelectivity(); }

  /**
   * Return a string representation of this vector projection.
   * @return A string representation of the projection's contents.
   */
  std::string ToString() const;

  /**
   * Print a string representation of this vector projection to the provided output stream.
   * @param os The stream where the string representation of this projection is written to.
   */
  void Dump(std::ostream &os) const;

  /**
   * Perform an integrity check on this vector projection instance. This is used in debug mode.
   */
  void CheckIntegrity() const;

 private:
  // Propagate the active TID list to child vectors, if necessary.
  void RefreshFilteredTupleIdList();

 private:
  // Vector containing column data for all columns in this projection.
  std::vector<std::unique_ptr<Vector>> columns_;

  // The list of active TIDs in the projection. Non-null only when tuples have
  // been filtered out.
  const TupleIdList *filter_;

  // The list of active TIDs in the projection. This is the source of truth and
  // will always represent the list of visible TIDs.
  TupleIdList owned_tid_list_;

  // If the vector projection allocates memory for all contained vectors, this
  // pointer owns that memory.
  std::unique_ptr<byte[]> owned_buffer_;
};

}  // namespace tpl::sql
