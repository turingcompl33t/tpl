#pragma once

#include "common/common.h"

namespace tpl::ast {

// The list of all builtin functions
// Args: internal name, function name
#define BUILTINS_LIST(F)                                        \
  /* Primitive <-> SQL */                                       \
  F(IntToSql, intToSql)                                         \
  F(BoolToSql, boolToSql)                                       \
  F(FloatToSql, floatToSql)                                     \
  F(DateToSql, dateToSql)                                       \
  F(StringToSql, stringToSql)                                   \
  F(SqlToBool, sqlToBool)                                       \
                                                                \
  /* SQL Functions */                                           \
  F(Like, like)                                                 \
                                                                \
  /* Thread State Container */                                  \
  F(ExecutionContextGetMemoryPool, execCtxGetMem)               \
  F(ThreadStateContainerInit, tlsInit)                          \
  F(ThreadStateContainerReset, tlsReset)                        \
  F(ThreadStateContainerIterate, tlsIterate)                    \
  F(ThreadStateContainerFree, tlsFree)                          \
                                                                \
  /* Table scans */                                             \
  F(TableIterInit, tableIterInit)                               \
  F(TableIterAdvance, tableIterAdvance)                         \
  F(TableIterGetVPI, tableIterGetVPI)                           \
  F(TableIterClose, tableIterClose)                             \
  F(TableIterParallel, iterateTableParallel)                    \
                                                                \
  /* VPI */                                                     \
  F(VPIIsFiltered, vpiIsFiltered)                               \
  F(VPIGetSelectedRowCount, vpiSelectedRowCount)                \
  F(VPIHasNext, vpiHasNext)                                     \
  F(VPIHasNextFiltered, vpiHasNextFiltered)                     \
  F(VPIAdvance, vpiAdvance)                                     \
  F(VPIAdvanceFiltered, vpiAdvanceFiltered)                     \
  F(VPISetPosition, vpiSetPosition)                             \
  F(VPISetPositionFiltered, vpiSetPositionFiltered)             \
  F(VPIMatch, vpiMatch)                                         \
  F(VPIReset, vpiReset)                                         \
  F(VPIResetFiltered, vpiResetFiltered)                         \
  F(VPIGetSmallInt, vpiGetSmallInt)                             \
  F(VPIGetInt, vpiGetInt)                                       \
  F(VPIGetBigInt, vpiGetBigInt)                                 \
  F(VPIGetReal, vpiGetReal)                                     \
  F(VPIGetDouble, vpiGetDouble)                                 \
  F(VPIGetDate, vpiGetDate)                                     \
  F(VPIGetString, vpiGetString)                                 \
  F(VPISetSmallInt, vpiSetSmallInt)                             \
  F(VPISetInt, vpiSetInt)                                       \
  F(VPISetBigInt, vpiSetBigInt)                                 \
  F(VPISetReal, vpiSetReal)                                     \
  F(VPISetDouble, vpiSetDouble)                                 \
  F(VPISetDate, vpiSetDate)                                     \
  F(VPISetString, vpiSetString)                                 \
                                                                \
  /* Hashing */                                                 \
  F(Hash, hash)                                                 \
                                                                \
  /* Filter Manager */                                          \
  F(FilterManagerInit, filterManagerInit)                       \
  F(FilterManagerInsertFilter, filterManagerInsertFilter)       \
  F(FilterManagerFinalize, filterManagerFinalize)               \
  F(FilterManagerRunFilters, filtersRun)                        \
  F(FilterManagerFree, filterManagerFree)                       \
                                                                \
  /* Vector Filter Executor */                                  \
  F(VectorFilterExecInit, filterExecInit)                       \
  F(VectorFilterExecEqual, filterExecEq)                        \
  F(VectorFilterExecGreaterThan, filterExecGt)                  \
  F(VectorFilterExecGreaterThanEqual, filterExecGe)             \
  F(VectorFilterExecLessThan, filterExecLt)                     \
  F(VectorFilterExecLessThanEqual, filterExecLe)                \
  F(VectorFilterExecNotEqual, filterExecNe)                     \
  F(VectorFilterExecFinish, filterExecFinish)                   \
  F(VectorFilterExecFree, filterExecFree)                       \
                                                                \
  /* Aggregations */                                            \
  F(AggHashTableInit, aggHTInit)                                \
  F(AggHashTableInsert, aggHTInsert)                            \
  F(AggHashTableLinkEntry, aggHTLink)                           \
  F(AggHashTableLookup, aggHTLookup)                            \
  F(AggHashTableProcessBatch, aggHTProcessBatch)                \
  F(AggHashTableMovePartitions, aggHTMoveParts)                 \
  F(AggHashTableParallelPartitionedScan, aggHTParallelPartScan) \
  F(AggHashTableFree, aggHTFree)                                \
  F(AggHashTableIterInit, aggHTIterInit)                        \
  F(AggHashTableIterHasNext, aggHTIterHasNext)                  \
  F(AggHashTableIterNext, aggHTIterNext)                        \
  F(AggHashTableIterGetRow, aggHTIterGetRow)                    \
  F(AggHashTableIterClose, aggHTIterClose)                      \
  F(AggPartIterHasNext, aggPartIterHasNext)                     \
  F(AggPartIterNext, aggPartIterNext)                           \
  F(AggPartIterGetHash, aggPartIterGetHash)                     \
  F(AggPartIterGetRow, aggPartIterGetRow)                       \
  F(AggPartIterGetRowEntry, aggPartIterGetRowEntry)             \
  F(AggInit, aggInit)                                           \
  F(AggAdvance, aggAdvance)                                     \
  F(AggMerge, aggMerge)                                         \
  F(AggReset, aggReset)                                         \
  F(AggResult, aggResult)                                       \
                                                                \
  /* Joins */                                                   \
  F(JoinHashTableInit, joinHTInit)                              \
  F(JoinHashTableInsert, joinHTInsert)                          \
  F(JoinHashTableBuild, joinHTBuild)                            \
  F(JoinHashTableBuildParallel, joinHTBuildParallel)            \
  F(JoinHashTableLookup, joinHTLookup)                          \
  F(JoinHashTableFree, joinHTFree)                              \
                                                                \
  /* Hash Table Entry Iterator (for hash joins) */              \
  F(HashTableEntryIterHasNext, htEntryIterHasNext)              \
  F(HashTableEntryIterGetRow, htEntryIterGetRow)                \
                                                                \
  /* Sorting */                                                 \
  F(SorterInit, sorterInit)                                     \
  F(SorterInsert, sorterInsert)                                 \
  F(SorterInsertTopK, sorterInsertTopK)                         \
  F(SorterInsertTopKFinish, sorterInsertTopKFinish)             \
  F(SorterSort, sorterSort)                                     \
  F(SorterSortParallel, sorterSortParallel)                     \
  F(SorterSortTopKParallel, sorterSortTopKParallel)             \
  F(SorterFree, sorterFree)                                     \
  F(SorterIterInit, sorterIterInit)                             \
  F(SorterIterHasNext, sorterIterHasNext)                       \
  F(SorterIterNext, sorterIterNext)                             \
  F(SorterIterGetRow, sorterIterGetRow)                         \
  F(SorterIterClose, sorterIterClose)                           \
                                                                \
  F(ResultBufferAllocOutRow, resultBufferAllocRow)              \
  F(ResultBufferFinalize, resultBufferFinalize)                 \
                                                                \
  /* Trig */                                                    \
  F(ACos, acos)                                                 \
  F(ASin, asin)                                                 \
  F(ATan, atan)                                                 \
  F(ATan2, atan2)                                               \
  F(Cos, cos)                                                   \
  F(Cot, cot)                                                   \
  F(Sin, sin)                                                   \
  F(Tan, tan)                                                   \
                                                                \
  /* Generic */                                                 \
  F(SizeOf, sizeOf)                                             \
  F(OffsetOf, offsetOf)                                         \
  F(PtrCast, ptrCast)

/**
 * An enumeration of all TPL builtin functions.
 */
enum class Builtin : uint8_t {
#define ENTRY(Name, ...) Name,
  BUILTINS_LIST(ENTRY)
#undef ENTRY
#define COUNT_OP(inst, ...) +1
      Last = -1 BUILTINS_LIST(COUNT_OP)
#undef COUNT_OP
};

/**
 * Helper class providing.
 */
class Builtins : public AllStatic {
 public:
  // The total number of builtin functions
  static const uint32_t kBuiltinsCount = static_cast<uint32_t>(Builtin ::Last) + 1;

  /**
   * @return The total number of builtin functions.
   */
  static constexpr uint32_t NumBuiltins() { return kBuiltinsCount; }

  /**
   * @return The name of the function associated with the given builtin enumeration.
   */
  static const char *GetFunctionName(Builtin builtin) {
    return kBuiltinFunctionNames[static_cast<uint8_t>(builtin)];
  }

 private:
  static const char *kBuiltinFunctionNames[];
};

}  // namespace tpl::ast
