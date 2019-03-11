#pragma once

#include <algorithm>
#include <cstdint>

#include "util/common.h"
#include "util/macros.h"
#include "vm/bytecode_operands.h"

namespace tpl::vm {

// clang-format off
// Creates instances of a given opcode for all integer primitive types
#define CREATE_FOR_INT_TYPES(func, op, ...)      \
  func(op##_##i8, __VA_ARGS__)                   \
  func(op##_##i16, __VA_ARGS__)                  \
  func(op##_##i32, __VA_ARGS__)                  \
  func(op##_##i64, __VA_ARGS__)                  \
  func(op##_##u8, __VA_ARGS__)                   \
  func(op##_##u16, __VA_ARGS__)                  \
  func(op##_##u32, __VA_ARGS__)                  \
  func(op##_##u64, __VA_ARGS__)
// clang-format on

// Creates instances of a given opcode for all floating-point primitive types
#define CREATE_FOR_FLOAT_TYPES(func, op) func(op, f32) func(op, f64)

// Creates instances of a given opcode for *ALL* primitive types
#define CREATE_FOR_ALL_TYPES(func, op, ...)   \
  CREATE_FOR_INT_TYPES(func, op, __VA_ARGS__) \
  CREATE_FOR_FLOAT_TYPES(func, op, __VA_ARGS__)

#define GET_BASE_FOR_INT_TYPES(op) (op##_i8)
#define GET_BASE_FOR_FLOAT_TYPES(op) (op##_f32)
#define GET_BASE_FOR_BOOL_TYPES(op) (op##_bool)

///
/// The master list of all bytecodes, flags and operands
///

// clang-format off
#define BYTECODE_LIST(F)                                                                                               \
  /* Primitive operations */                                                                                           \
  CREATE_FOR_INT_TYPES(F, Add, OperandType::Local, OperandType::Local, OperandType::Local)                             \
  CREATE_FOR_INT_TYPES(F, Sub, OperandType::Local, OperandType::Local, OperandType::Local)                             \
  CREATE_FOR_INT_TYPES(F, Mul, OperandType::Local, OperandType::Local, OperandType::Local)                             \
  CREATE_FOR_INT_TYPES(F, Div, OperandType::Local, OperandType::Local, OperandType::Local)                             \
  CREATE_FOR_INT_TYPES(F, Rem, OperandType::Local, OperandType::Local, OperandType::Local)                             \
  CREATE_FOR_INT_TYPES(F, BitAnd, OperandType::Local, OperandType::Local, OperandType::Local)                          \
  CREATE_FOR_INT_TYPES(F, BitOr, OperandType::Local, OperandType::Local, OperandType::Local)                           \
  CREATE_FOR_INT_TYPES(F, BitXor, OperandType::Local, OperandType::Local, OperandType::Local)                          \
  CREATE_FOR_INT_TYPES(F, Neg, OperandType::Local, OperandType::Local)                                                 \
  CREATE_FOR_INT_TYPES(F, BitNeg, OperandType::Local, OperandType::Local)                                              \
  CREATE_FOR_INT_TYPES(F, GreaterThan, OperandType::Local, OperandType::Local, OperandType::Local)                     \
  CREATE_FOR_INT_TYPES(F, GreaterThanEqual, OperandType::Local, OperandType::Local, OperandType::Local)                \
  CREATE_FOR_INT_TYPES(F, Equal, OperandType::Local, OperandType::Local, OperandType::Local)                           \
  CREATE_FOR_INT_TYPES(F, LessThan, OperandType::Local, OperandType::Local, OperandType::Local)                        \
  CREATE_FOR_INT_TYPES(F, LessThanEqual, OperandType::Local, OperandType::Local, OperandType::Local)                   \
  CREATE_FOR_INT_TYPES(F, NotEqual, OperandType::Local, OperandType::Local, OperandType::Local)                        \
                                                                                                                       \
  /* Branching */                                                                                                      \
  F(Jump, OperandType::JumpOffset)                                                                                     \
  F(JumpIfTrue, OperandType::Local, OperandType::JumpOffset)                                                           \
  F(JumpIfFalse, OperandType::Local, OperandType::JumpOffset)                                                          \
                                                                                                                       \
  /* Memory/pointer operations */                                                                                      \
  F(Deref1, OperandType::Local, OperandType::Local)                                                                    \
  F(Deref2, OperandType::Local, OperandType::Local)                                                                    \
  F(Deref4, OperandType::Local, OperandType::Local)                                                                    \
  F(Deref8, OperandType::Local, OperandType::Local)                                                                    \
  F(DerefN, OperandType::Local, OperandType::Local, OperandType::UImm4)                                                \
  F(Assign1, OperandType::Local, OperandType::Local)                                                                   \
  F(Assign2, OperandType::Local, OperandType::Local)                                                                   \
  F(Assign4, OperandType::Local, OperandType::Local)                                                                   \
  F(Assign8, OperandType::Local, OperandType::Local)                                                                   \
  F(AssignImm1, OperandType::Local, OperandType::Imm1)                                                                 \
  F(AssignImm2, OperandType::Local, OperandType::Imm2)                                                                 \
  F(AssignImm4, OperandType::Local, OperandType::Imm4)                                                                 \
  F(AssignImm8, OperandType::Local, OperandType::Imm8)                                                                 \
  F(Lea, OperandType::Local, OperandType::Local, OperandType::Imm4)                                                    \
  F(LeaScaled, OperandType::Local, OperandType::Local, OperandType::Local, OperandType::Imm4, OperandType::Imm4)       \
                                                                                                                       \
  /* Function calls */                                                                                                 \
  F(Call, OperandType::FunctionId, OperandType::LocalCount)                                                            \
  F(Return)                                                                                                            \
                                                                                                                       \
  /* Table Vector Iterator */                                                                                          \
  F(TableVectorIteratorInit, OperandType::Local, OperandType::UImm2)                                                   \
  F(TableVectorIteratorNext, OperandType::Local, OperandType::Local)                                                   \
  F(TableVectorIteratorClose, OperandType::Local)                                                                      \
  F(TableVectorIteratorGetVPI, OperandType::Local, OperandType::Local)                                                 \
                                                                                                                       \
  /* Vector Projection Iterator (VPI) */                                                                               \
  F(VPIHasNext, OperandType::Local, OperandType::Local)                                                                \
  F(VPIAdvance, OperandType::Local)                                                                                    \
  F(VPIReset, OperandType::Local)                                                                                      \
  F(VPIGetSmallInt, OperandType::Local, OperandType::Local, OperandType::UImm4)                                        \
  F(VPIGetInteger, OperandType::Local, OperandType::Local, OperandType::UImm4)                                         \
  F(VPIGetBigInt, OperandType::Local, OperandType::Local, OperandType::UImm4)                                          \
  F(VPIGetDecimal, OperandType::Local, OperandType::Local, OperandType::UImm4)                                         \
  F(VPIGetSmallIntNull, OperandType::Local, OperandType::Local, OperandType::UImm4)                                    \
  F(VPIGetIntegerNull, OperandType::Local, OperandType::Local, OperandType::UImm4)                                     \
  F(VPIGetBigIntNull, OperandType::Local, OperandType::Local, OperandType::UImm4)                                      \
  F(VPIGetDecimalNull, OperandType::Local, OperandType::Local, OperandType::UImm4)                                     \
  F(VPIFilterEqual, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)                     \
  F(VPIFilterGreaterThan, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)               \
  F(VPIFilterGreaterThanEqual, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)          \
  F(VPIFilterLessThan, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)                  \
  F(VPIFilterLessThanEqual, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)             \
  F(VPIFilterNotEqual, OperandType::Local, OperandType::Local, OperandType::UImm4, OperandType::Imm8)                  \
                                                                                                                       \
  /* SQL type comparisons */                                                                                           \
  F(ForceBoolTruth, OperandType::Local, OperandType::Local)                                                            \
  F(InitInteger, OperandType::Local, OperandType::Local)                                                               \
  F(LessThanInteger, OperandType::Local, OperandType::Local, OperandType::Local)                                       \
  F(LessThanEqualInteger, OperandType::Local, OperandType::Local, OperandType::Local)                                  \
  F(GreaterThanInteger, OperandType::Local, OperandType::Local, OperandType::Local)                                    \
  F(GreaterThanEqualInteger, OperandType::Local, OperandType::Local, OperandType::Local)                               \
  F(EqualInteger, OperandType::Local, OperandType::Local, OperandType::Local)                                          \
  F(NotEqualInteger, OperandType::Local, OperandType::Local, OperandType::Local)                                       \
                                                                                                                       \
  /* Aggregations */                                                                                                   \
  F(CountAggregateInit, OperandType::Local)                                                                            \
  F(CountAggregateAdvance, OperandType::Local, OperandType::Local)                                                     \
  F(CountAggregateMerge, OperandType::Local, OperandType::Local)                                                       \
  F(CountAggregateReset, OperandType::Local)                                                                           \
  F(CountAggregateGetResult, OperandType::Local, OperandType::Local)                                                   \
  F(CountAggregateFree, OperandType::Local)                                                                            \
  F(CountStarAggregateInit, OperandType::Local)                                                                        \
  F(CountStarAggregateAdvance, OperandType::Local, OperandType::Local)                                                 \
  F(CountStarAggregateMerge, OperandType::Local, OperandType::Local)                                                   \
  F(CountStarAggregateReset, OperandType::Local)                                                                       \
  F(CountStarAggregateGetResult, OperandType::Local, OperandType::Local)                                               \
  F(CountStarAggregateFree, OperandType::Local)                                                                        \
                                                                                                                       \
  /* Joins */                                                                                                          \
  F(JoinHashTableAllocTuple, OperandType::Local, OperandType::Local, OperandType::Local)                               \
  F(JoinHashTableBuild, OperandType::Local)                                                                            \

// clang-format on

/// The single enumeration of all possible bytecode instructions
enum class Bytecode : u32 {
#define DECLARE_OP(inst, ...) inst,
  BYTECODE_LIST(DECLARE_OP)
#undef DECLARE_OP
#define COUNT_OP(inst, ...) +1
      Last = -1 BYTECODE_LIST(COUNT_OP)
#undef COUNT_OP
};

/// Helper class for querying/interacting with bytecode instructions
class Bytecodes {
 public:
  // The total number of bytecode instructions
  static const u32 kBytecodeCount = static_cast<u32>(Bytecode::Last) + 1;

  // Return the maximum length of any bytecode instruction in bytes
  static u32 MaxBytecodeNameLength();

  // Returns the string representation of the given bytecode
  static const char *ToString(Bytecode bytecode) {
    return kBytecodeNames[static_cast<u32>(bytecode)];
  }

  // Return the number of operands a bytecode accepts
  static u32 NumOperands(Bytecode bytecode) {
    return kBytecodeOperandCounts[static_cast<u32>(bytecode)];
  }

  // Return an array of the operand types to the given bytecode
  static const OperandType *GetOperandTypes(Bytecode bytecode) {
    return kBytecodeOperandTypes[static_cast<u32>(bytecode)];
  }

  // Return an array of the sizes of all operands to the given bytecode
  static const OperandSize *GetOperandSizes(Bytecode bytecode) {
    return kBytecodeOperandSizes[static_cast<u32>(bytecode)];
  }

  // Return the type of the Nth operand to the given bytecode
  static OperandType GetNthOperandType(Bytecode bytecode, u32 operand_index) {
    TPL_ASSERT(operand_index < NumOperands(bytecode),
               "Accessing out-of-bounds operand number for bytecode");
    return GetOperandTypes(bytecode)[operand_index];
  }

  // Return the type of the Nth operand to the given bytecode
  static OperandSize GetNthOperandSize(Bytecode bytecode, u32 operand_index) {
    TPL_ASSERT(operand_index < NumOperands(bytecode),
               "Accessing out-of-bounds operand number for bytecode");
    return GetOperandSizes(bytecode)[operand_index];
  }

  // Return the offset of the Nth operand of the given bytecode
  static u32 GetNthOperandOffset(Bytecode bytecode, u32 operand_index);

  // Return the name of the bytecode handler function for this bytecode
  static const char *GetBytecodeHandlerName(Bytecode bytecode) {
    return kBytecodeHandlerName[ToByte(bytecode)];
  }

  // Converts the given bytecode to a single-byte representation
  static std::underlying_type_t<Bytecode> ToByte(Bytecode bytecode) {
    TPL_ASSERT(bytecode <= Bytecode::Last, "Invalid bytecode");
    return static_cast<std::underlying_type_t<Bytecode>>(bytecode);
  }

  // Converts the given unsigned byte into the associated bytecode
  static Bytecode FromByte(std::underlying_type_t<Bytecode> val) {
    auto bytecode = static_cast<Bytecode>(val);
    TPL_ASSERT(bytecode <= Bytecode::Last, "Invalid bytecode");
    return bytecode;
  }

  static bool IsJump(Bytecode bytecode) {
    return (bytecode == Bytecode::Jump || bytecode == Bytecode::JumpIfFalse ||
            bytecode == Bytecode::JumpIfTrue);
  }

  static bool IsCall(Bytecode bytecode) { return bytecode == Bytecode::Call; }

  static bool IsTerminal(Bytecode bytecode) {
    return bytecode == Bytecode::Jump || bytecode == Bytecode::Return;
  }

 private:
  static const char *kBytecodeNames[];
  static u32 kBytecodeOperandCounts[];
  static const OperandType *kBytecodeOperandTypes[];
  static const OperandSize *kBytecodeOperandSizes[];
  static const char *kBytecodeHandlerName[];
};

}  // namespace tpl::vm
