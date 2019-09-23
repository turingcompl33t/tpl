#include "vm/vm.h"

#include <numeric>
#include <string>

#include "common/common.h"
#include "sql/value.h"
#include "util/memory.h"
#include "vm/bytecode_function_info.h"
#include "vm/bytecode_handlers.h"
#include "vm/module.h"

namespace tpl::vm {

/**
 * An execution frame where all function's local variables and parameters live
 * for the duration of the function's lifetime.
 */
class VM::Frame {
  friend class VM;

 public:
  Frame(uint8_t *frame_data, std::size_t frame_size)
      : frame_data_(frame_data), frame_size_(frame_size) {
    TPL_ASSERT(frame_data_ != nullptr, "Frame data cannot be null");
    TPL_ASSERT(frame_size_ >= 0, "Frame size must be >= 0");
    (void)frame_size_;
  }

  void *PtrToLocalAt(const LocalVar local) const {
    EnsureInFrame(local);
    return frame_data_ + local.GetOffset();
  }

  /**
   * Access the local variable at the given index in the fame. @em index is an
   * encoded LocalVar that contains both the byte offset of the variable to
   * load and the access mode, i.e., whether the local variable is accessed
   * accessed by address or value.
   * @tparam T The type of the variable the user expects
   * @param index The encoded index into the frame where the variable is
   * @return The value of the variable. Note that this is copied!
   */
  template <typename T>
  T LocalAt(uint32_t index) const {
    LocalVar local = LocalVar::Decode(index);
    const auto val = reinterpret_cast<uintptr_t>(PtrToLocalAt(local));
    if (local.GetAddressMode() == LocalVar::AddressMode::Value) {
      return *reinterpret_cast<T *>(val);
    }
    return (T)(val);  // NOLINT (both static/reinterpret cast semantics)
  }

 private:
#ifndef NDEBUG
  // Ensure the local variable is valid
  void EnsureInFrame(LocalVar var) const {
    if (var.GetOffset() >= frame_size_) {
      std::string error_msg = fmt::format("Accessing local at offset {}, beyond frame of size {}",
                                          var.GetOffset(), frame_size_);
      LOG_ERROR("{}", error_msg);
      throw std::runtime_error(error_msg);
    }
  }
#else
  void EnsureInFrame(UNUSED LocalVar var) const {}
#endif

 private:
  uint8_t *frame_data_;
  std::size_t frame_size_;
};

// ---------------------------------------------------------
// Virtual Machine
// ---------------------------------------------------------

// The maximum amount of stack to use. If the function requires more than 16K
// bytes, acquire space from the heap.
static constexpr const uint32_t kMaxStackAllocSize = 1ull << 14ull;
// A soft-maximum amount of stack to use. If a function's frame requires more
// than 4K (the soft max), try the stack and fallback to heap. If the function
// requires less, use the stack.
static constexpr const uint32_t kSoftMaxStackAllocSize = 1ull << 12ull;

VM::VM(const Module *module) : module_(module) {}

// static
void VM::InvokeFunction(const Module *module, const FunctionId func_id, const uint8_t args[]) {
  // The function's info
  const FunctionInfo *func_info = module->GetFuncInfoById(func_id);
  TPL_ASSERT(func_info != nullptr, "Function doesn't exist in module!");
  const std::size_t frame_size = func_info->frame_size();

  // Let's try to get some space
  bool used_heap = false;
  uint8_t *raw_frame = nullptr;
  if (frame_size > kMaxStackAllocSize) {
    used_heap = true;
    raw_frame = static_cast<uint8_t *>(util::MallocAligned(frame_size, alignof(uint64_t)));
  } else if (frame_size > kSoftMaxStackAllocSize) {
    // TODO(pmenon): Check stack before allocation
    raw_frame = static_cast<uint8_t *>(alloca(frame_size));
  } else {
    raw_frame = static_cast<uint8_t *>(alloca(frame_size));
  }

  // Copy args into frame
  std::memcpy(raw_frame + func_info->params_start_pos(), args, func_info->params_size());

  LOG_DEBUG("Executing function '{}'", func_info->name());

  // Let's go. First, create the virtual machine instance.
  VM vm(module);

  // Now get the bytecode for the function and fire it off
  const uint8_t *bytecode = module->bytecode_module()->GetBytecodeForFunction(*func_info);
  TPL_ASSERT(bytecode != nullptr, "Bytecode cannot be null");
  Frame frame(raw_frame, frame_size);
  vm.Interpret(bytecode, &frame);

  // Cleanup
  if (used_heap) {
    std::free(raw_frame);
  }
}

namespace {

template <typename T>
inline ALWAYS_INLINE T Read(const uint8_t **ip) {
  static_assert(std::is_integral_v<T>,
                "Read() should only be used to read primitive integer types "
                "directly from the bytecode instruction stream");
  auto ret = *reinterpret_cast<const T *>(*ip);
  (*ip) += sizeof(T);
  return ret;
}

template <typename T>
inline ALWAYS_INLINE T Peek(const uint8_t **ip) {
  static_assert(std::is_integral_v<T>,
                "Peek() should only be used to read primitive integer types "
                "directly from the bytecode instruction stream");
  return *reinterpret_cast<const T *>(*ip);
}

}  // namespace

// NOLINTNEXTLINE(google-readability-function-size,readability-function-size)
void VM::Interpret(const uint8_t *ip, Frame *frame) {
  static void *kDispatchTable[] = {
#define ENTRY(name, ...) &&op_##name,
      BYTECODE_LIST(ENTRY)
#undef ENTRY
  };

#ifdef TPL_DEBUG_TRACE_INSTRUCTIONS
#define DEBUG_TRACE_INSTRUCTIONS(op)                                        \
  do {                                                                      \
    auto bytecode = Bytecodes::FromByte(op);                                \
    bytecode_counts_[op]++;                                                 \
    LOG_INFO("{0:p}: {1:s}", ip - sizeof(std::underlying_type_t<Bytecode>), \
             Bytecodes::ToString(bytecode));                                \
  } while (false)
#else
#define DEBUG_TRACE_INSTRUCTIONS(op) (void)op
#endif

  // TODO(pmenon): Should these READ/PEEK macros take in a vm::OperandType so
  // that we can infer primitive types using traits? This minimizes number of
  // changes if the underlying offset/bytecode/register sizes changes?
#define PEEK_JMP_OFFSET() Peek<int32_t>(&ip)
#define READ_IMM1() Read<int8_t>(&ip)
#define READ_IMM2() Read<int16_t>(&ip)
#define READ_IMM4() Read<int32_t>(&ip)
#define READ_IMM8() Read<int64_t>(&ip)
#define READ_UIMM2() Read<uint16_t>(&ip)
#define READ_UIMM4() Read<uint32_t>(&ip)
#define READ_JMP_OFFSET() READ_IMM4()
#define READ_LOCAL_ID() Read<uint32_t>(&ip)
#define READ_OP() Read<std::underlying_type_t<Bytecode>>(&ip)
#define READ_FUNC_ID() READ_UIMM2()

#define OP(name) op_##name
#define DISPATCH_NEXT()           \
  do {                            \
    auto op = READ_OP();          \
    DEBUG_TRACE_INSTRUCTIONS(op); \
    goto *kDispatchTable[op];     \
  } while (false)

  /*****************************************************************************
   *
   * Below this comment begins the primary section of TPL's register-based
   * virtual machine (VM) dispatch area. The VM uses indirect threaded
   * interpretation; each bytecode handler's label is statically generated and
   * stored in @ref kDispatchTable at server compile time. Bytecode handler
   * logic is written as a case using the CASE_OP macro. Handlers can read from
   * and write to registers using the local execution frame's register file
   * (i.e., through @ref Frame::LocalAt()).
   *
   * Upon entry, the instruction pointer (IP) points to the first bytecode of
   * function that is running. The READ_* macros can be used to directly read
   * values from the bytecode stream. The READ_* macros read values from the
   * bytecode stream and advance the IP whereas the PEEK_* macros do only the
   * former, leaving the IP unmodified.
   *
   * IMPORTANT:
   * ----------
   * Bytecode handler code here should only be simple register/IP manipulation
   * (i.e., reading from and writing to registers). Actual full-blown bytecode
   * logic must be implemented externally and invoked from stubs here. This is a
   * strict requirement necessary because it makes code generation to LLVM much
   * simpler.
   *
   ****************************************************************************/

  // Jump to the first instruction
  DISPATCH_NEXT();

  // -------------------------------------------------------
  // Primitive comparison operations
  // -------------------------------------------------------

#define DO_GEN_COMPARISON(op, type)                       \
  OP(op##_##type) : {                                     \
    auto *dest = frame->LocalAt<bool *>(READ_LOCAL_ID()); \
    auto lhs = frame->LocalAt<type>(READ_LOCAL_ID());     \
    auto rhs = frame->LocalAt<type>(READ_LOCAL_ID());     \
    Op##op##_##type(dest, lhs, rhs);                      \
    DISPATCH_NEXT();                                      \
  }
#define GEN_COMPARISON_TYPES(type, ...)     \
  DO_GEN_COMPARISON(GreaterThan, type)      \
  DO_GEN_COMPARISON(GreaterThanEqual, type) \
  DO_GEN_COMPARISON(Equal, type)            \
  DO_GEN_COMPARISON(LessThan, type)         \
  DO_GEN_COMPARISON(LessThanEqual, type)    \
  DO_GEN_COMPARISON(NotEqual, type)

  INT_TYPES(GEN_COMPARISON_TYPES)
#undef GEN_COMPARISON_TYPES
#undef DO_GEN_COMPARISON

  // -------------------------------------------------------
  // Primitive arithmetic and binary operations
  // -------------------------------------------------------

#define DO_GEN_ARITHMETIC_OP(op, test, type)              \
  OP(op##_##type) : {                                     \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    auto lhs = frame->LocalAt<type>(READ_LOCAL_ID());     \
    auto rhs = frame->LocalAt<type>(READ_LOCAL_ID());     \
    if ((test) && rhs == 0u) {                            \
      /* TODO(pmenon): Proper error */                    \
      LOG_ERROR("Division by zero error!");               \
    }                                                     \
    Op##op##_##type(dest, lhs, rhs);                      \
    DISPATCH_NEXT();                                      \
  }
#define GEN_ARITHMETIC_OP(type, ...)        \
  DO_GEN_ARITHMETIC_OP(Add, false, type)    \
  DO_GEN_ARITHMETIC_OP(Sub, false, type)    \
  DO_GEN_ARITHMETIC_OP(Mul, false, type)    \
  DO_GEN_ARITHMETIC_OP(Div, true, type)     \
  DO_GEN_ARITHMETIC_OP(Rem, true, type)     \
  DO_GEN_ARITHMETIC_OP(BitAnd, false, type) \
  DO_GEN_ARITHMETIC_OP(BitOr, false, type)  \
  DO_GEN_ARITHMETIC_OP(BitXor, false, type)

  INT_TYPES(GEN_ARITHMETIC_OP)
#undef GEN_ARITHMETIC_OP
#undef DO_GEN_ARITHMETIC_OP

  // -------------------------------------------------------
  // Bitwise and integer negation
  // -------------------------------------------------------

#define GEN_NEG_OP(type, ...)                             \
  OP(Neg##_##type) : {                                    \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    auto input = frame->LocalAt<type>(READ_LOCAL_ID());   \
    OpNeg##_##type(dest, input);                          \
    DISPATCH_NEXT();                                      \
  }                                                       \
  OP(BitNeg##_##type) : {                                 \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    auto input = frame->LocalAt<type>(READ_LOCAL_ID());   \
    OpBitNeg##_##type(dest, input);                       \
    DISPATCH_NEXT();                                      \
  }

  INT_TYPES(GEN_NEG_OP)
#undef GEN_NEG_OP

  OP(Not) : {
    auto *dest = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto input = frame->LocalAt<bool>(READ_LOCAL_ID());
    OpNot(dest, input);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Jumps
  // -------------------------------------------------------

  OP(Jump) : {
    auto skip = PEEK_JMP_OFFSET();
    if (TPL_LIKELY(OpJump())) {
      ip += skip;
    }
    DISPATCH_NEXT();
  }

  OP(JumpIfTrue) : {
    auto cond = frame->LocalAt<bool>(READ_LOCAL_ID());
    auto skip = PEEK_JMP_OFFSET();
    if (OpJumpIfTrue(cond)) {
      ip += skip;
    } else {
      READ_JMP_OFFSET();
    }
    DISPATCH_NEXT();
  }

  OP(JumpIfFalse) : {
    auto cond = frame->LocalAt<bool>(READ_LOCAL_ID());
    auto skip = PEEK_JMP_OFFSET();
    if (OpJumpIfFalse(cond)) {
      ip += skip;
    } else {
      READ_JMP_OFFSET();
    }
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Low-level memory operations
  // -------------------------------------------------------

  OP(IsNullPtr) : {
    auto *result = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *input_ptr = frame->LocalAt<const void *>(READ_LOCAL_ID());
    OpIsNullPtr(result, input_ptr);
    DISPATCH_NEXT();
  }

  OP(IsNotNullPtr) : {
    auto *result = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *input_ptr = frame->LocalAt<const void *>(READ_LOCAL_ID());
    OpIsNotNullPtr(result, input_ptr);
    DISPATCH_NEXT();
  }

#define GEN_DEREF(type, size)                             \
  OP(Deref##size) : {                                     \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    auto *src = frame->LocalAt<type *>(READ_LOCAL_ID());  \
    OpDeref##size(dest, src);                             \
    DISPATCH_NEXT();                                      \
  }
  GEN_DEREF(int8_t, 1);
  GEN_DEREF(int16_t, 2);
  GEN_DEREF(int32_t, 4);
  GEN_DEREF(int64_t, 8);
#undef GEN_DEREF

  OP(DerefN) : {
    auto *dest = frame->LocalAt<byte *>(READ_LOCAL_ID());
    auto *src = frame->LocalAt<byte *>(READ_LOCAL_ID());
    auto len = READ_UIMM4();
    OpDerefN(dest, src, len);
    DISPATCH_NEXT();
  }

#define GEN_ASSIGN(type, size)                            \
  OP(Assign##size) : {                                    \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    auto src = frame->LocalAt<type>(READ_LOCAL_ID());     \
    OpAssign##size(dest, src);                            \
    DISPATCH_NEXT();                                      \
  }                                                       \
  OP(AssignImm##size) : {                                 \
    auto *dest = frame->LocalAt<type *>(READ_LOCAL_ID()); \
    OpAssignImm##size(dest, READ_IMM##size());            \
    DISPATCH_NEXT();                                      \
  }
  GEN_ASSIGN(int8_t, 1);
  GEN_ASSIGN(int16_t, 2);
  GEN_ASSIGN(int32_t, 4);
  GEN_ASSIGN(int64_t, 8);
#undef GEN_ASSIGN

  OP(Lea) : {
    auto **dest = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *src = frame->LocalAt<byte *>(READ_LOCAL_ID());
    auto offset = READ_UIMM4();
    OpLea(dest, src, offset);
    DISPATCH_NEXT();
  }

  OP(LeaScaled) : {
    auto **dest = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *src = frame->LocalAt<byte *>(READ_LOCAL_ID());
    auto index = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto scale = READ_UIMM4();
    auto offset = READ_UIMM4();
    OpLeaScaled(dest, src, index, scale, offset);
    DISPATCH_NEXT();
  }

  OP(Call) : {
    ip = ExecuteCall(ip, frame);
    DISPATCH_NEXT();
  }

  OP(Return) : {
    OpReturn();
    return;
  }

  // -------------------------------------------------------
  // Execution Context
  // -------------------------------------------------------

  OP(ExecutionContextGetMemoryPool) : {
    auto *memory_pool = frame->LocalAt<sql::MemoryPool **>(READ_LOCAL_ID());
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    OpExecutionContextGetMemoryPool(memory_pool, exec_ctx);
    DISPATCH_NEXT();
  }

  OP(ThreadStateContainerInit) : {
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto *memory = frame->LocalAt<tpl::sql::MemoryPool *>(READ_LOCAL_ID());
    OpThreadStateContainerInit(thread_state_container, memory);
    DISPATCH_NEXT();
  }

  OP(ThreadStateContainerIterate) : {
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto ctx = frame->LocalAt<void *>(READ_LOCAL_ID());
    auto iterate_fn_id = READ_FUNC_ID();

    auto iterate_fn = reinterpret_cast<sql::ThreadStateContainer::IterateFn>(
        module_->GetRawFunctionImpl(iterate_fn_id));
    OpThreadStateContainerIterate(thread_state_container, ctx, iterate_fn);
    DISPATCH_NEXT();
  }

  OP(ThreadStateContainerReset) : {
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto size = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto init_fn_id = READ_FUNC_ID();
    auto destroy_fn_id = READ_FUNC_ID();
    auto *ctx = frame->LocalAt<void *>(READ_LOCAL_ID());

    auto init_fn = reinterpret_cast<sql::ThreadStateContainer::InitFn>(
        module_->GetRawFunctionImpl(init_fn_id));
    auto destroy_fn = reinterpret_cast<sql::ThreadStateContainer::DestroyFn>(
        module_->GetRawFunctionImpl(destroy_fn_id));
    OpThreadStateContainerReset(thread_state_container, size, init_fn, destroy_fn, ctx);
    DISPATCH_NEXT();
  }

  OP(ThreadStateContainerFree) : {
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    OpThreadStateContainerFree(thread_state_container);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Table Vector and Vector Projection Iterator (VPI) ops
  // -------------------------------------------------------

  OP(TableVectorIteratorInit) : {
    auto *iter = frame->LocalAt<sql::TableVectorIterator *>(READ_LOCAL_ID());
    auto table_id = READ_UIMM2();
    OpTableVectorIteratorInit(iter, table_id);
    DISPATCH_NEXT();
  }

  OP(TableVectorIteratorPerformInit) : {
    auto *iter = frame->LocalAt<sql::TableVectorIterator *>(READ_LOCAL_ID());
    OpTableVectorIteratorPerformInit(iter);
    DISPATCH_NEXT();
  }

  OP(TableVectorIteratorNext) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::TableVectorIterator *>(READ_LOCAL_ID());
    OpTableVectorIteratorNext(has_more, iter);
    DISPATCH_NEXT();
  }

  OP(TableVectorIteratorFree) : {
    auto *iter = frame->LocalAt<sql::TableVectorIterator *>(READ_LOCAL_ID());
    OpTableVectorIteratorFree(iter);
    DISPATCH_NEXT();
  }

  OP(TableVectorIteratorGetVPI) : {
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator **>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::TableVectorIterator *>(READ_LOCAL_ID());
    OpTableVectorIteratorGetVPI(vpi, iter);
    DISPATCH_NEXT();
  }

  OP(ParallelScanTable) : {
    auto table_id = READ_UIMM2();
    auto query_state = frame->LocalAt<void *>(READ_LOCAL_ID());
    auto thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto scan_fn_id = READ_FUNC_ID();

    auto scan_fn =
        reinterpret_cast<sql::TableVectorIterator::ScanFn>(module_->GetRawFunctionImpl(scan_fn_id));
    OpParallelScanTable(table_id, query_state, thread_state_container, scan_fn);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // VPI iteration operations
  // -------------------------------------------------------

  OP(VPIIsFiltered) : {
    auto *is_filtered = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIIsFiltered(is_filtered, iter);
    DISPATCH_NEXT();
  }

  OP(VPIGetSelectedRowCount) : {
    auto *count = frame->LocalAt<uint32_t *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIGetSelectedRowCount(count, iter);
    DISPATCH_NEXT();
  }

  OP(VPIHasNext) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIHasNext(has_more, iter);
    DISPATCH_NEXT();
  }

  OP(VPIHasNextFiltered) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIHasNextFiltered(has_more, iter);
    DISPATCH_NEXT();
  }

  OP(VPIAdvance) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIAdvance(iter);
    DISPATCH_NEXT();
  }

  OP(VPIAdvanceFiltered) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIAdvanceFiltered(iter);
    DISPATCH_NEXT();
  }

  OP(VPISetPosition) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto index = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpVPISetPosition(iter, index);
    DISPATCH_NEXT();
  }

  OP(VPISetPositionFiltered) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto index = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpVPISetPositionFiltered(iter, index);
    DISPATCH_NEXT();
  }

  OP(VPIMatch) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto match = frame->LocalAt<bool>(READ_LOCAL_ID());
    OpVPIMatch(iter, match);
    DISPATCH_NEXT();
  }

  OP(VPIReset) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIReset(iter);
    DISPATCH_NEXT();
  }

  OP(VPIResetFiltered) : {
    auto *iter = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVPIResetFiltered(iter);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // VPI element access
  // -------------------------------------------------------

#define GEN_VPI_ACCESS(NAME, CPP_TYPE)                                            \
  OP(VPIGet##NAME) : {                                                            \
    auto *result = frame->LocalAt<CPP_TYPE *>(READ_LOCAL_ID());                   \
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID()); \
    auto col_idx = READ_UIMM4();                                                  \
    OpVPIGet##NAME(result, vpi, col_idx);                                         \
    DISPATCH_NEXT();                                                              \
  }                                                                               \
  OP(VPIGet##NAME##Null) : {                                                      \
    auto *result = frame->LocalAt<CPP_TYPE *>(READ_LOCAL_ID());                   \
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID()); \
    auto col_idx = READ_UIMM4();                                                  \
    OpVPIGet##NAME##Null(result, vpi, col_idx);                                   \
    DISPATCH_NEXT();                                                              \
  }                                                                               \
  OP(VPISet##NAME) : {                                                            \
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID()); \
    auto *input = frame->LocalAt<CPP_TYPE *>(READ_LOCAL_ID());                    \
    auto col_idx = READ_UIMM4();                                                  \
    OpVPISet##NAME(vpi, input, col_idx);                                          \
    DISPATCH_NEXT();                                                              \
  }                                                                               \
  OP(VPISet##NAME##Null) : {                                                      \
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID()); \
    auto *input = frame->LocalAt<CPP_TYPE *>(READ_LOCAL_ID());                    \
    auto col_idx = READ_UIMM4();                                                  \
    OpVPISet##NAME##Null(vpi, input, col_idx);                                    \
    DISPATCH_NEXT();                                                              \
  }
  GEN_VPI_ACCESS(SmallInt, sql::Integer)
  GEN_VPI_ACCESS(Integer, sql::Integer)
  GEN_VPI_ACCESS(BigInt, sql::Integer)
  GEN_VPI_ACCESS(Real, sql::Real)
  GEN_VPI_ACCESS(Double, sql::Real)
  GEN_VPI_ACCESS(Decimal, sql::Decimal)
  GEN_VPI_ACCESS(Date, sql::DateVal)
  GEN_VPI_ACCESS(String, sql::StringVal)
#undef GEN_VPI_ACCESS

  // ------------------------------------------------------
  // Hashing
  // ------------------------------------------------------

  OP(HashInt) : {
    auto *hash_val = frame->LocalAt<hash_t *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    auto seed = frame->LocalAt<const hash_t>(READ_LOCAL_ID());
    OpHashInt(hash_val, input, seed);
    DISPATCH_NEXT();
  }

  OP(HashReal) : {
    auto *hash_val = frame->LocalAt<hash_t *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    auto seed = frame->LocalAt<const hash_t>(READ_LOCAL_ID());
    OpHashReal(hash_val, input, seed);
    DISPATCH_NEXT();
  }

  OP(HashString) : {
    auto *hash_val = frame->LocalAt<hash_t *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto seed = frame->LocalAt<const hash_t>(READ_LOCAL_ID());
    OpHashString(hash_val, input, seed);
    DISPATCH_NEXT();
  }

  OP(HashCombine) : {
    auto *hash_val = frame->LocalAt<hash_t *>(READ_LOCAL_ID());
    auto new_hash_val = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    OpHashCombine(hash_val, new_hash_val);
    DISPATCH_NEXT();
  }

  // ------------------------------------------------------
  // Filter Manager
  // ------------------------------------------------------

  OP(FilterManagerInit) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    OpFilterManagerInit(filter_manager);
    DISPATCH_NEXT();
  }

  OP(FilterManagerStartNewClause) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    OpFilterManagerStartNewClause(filter_manager);
    DISPATCH_NEXT();
  }

  OP(FilterManagerInsertFlavor) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    auto func_id = READ_FUNC_ID();
    auto fn = reinterpret_cast<sql::FilterManager::MatchFn>(module_->GetRawFunctionImpl(func_id));
    OpFilterManagerInsertFlavor(filter_manager, fn);
    DISPATCH_NEXT();
  }

  OP(FilterManagerFinalize) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    OpFilterManagerFinalize(filter_manager);
    DISPATCH_NEXT();
  }

  OP(FilterManagerRunFilters) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpFilterManagerRunFilters(filter_manager, vpi);
    DISPATCH_NEXT();
  }

  OP(FilterManagerFree) : {
    auto *filter_manager = frame->LocalAt<sql::FilterManager *>(READ_LOCAL_ID());
    OpFilterManagerFree(filter_manager);
    DISPATCH_NEXT();
  }

  // ------------------------------------------------------
  // Vector Filter Executor
  // ------------------------------------------------------

  OP(VectorFilterExecuteInit) : {
    auto *filter_exec = frame->LocalAt<sql::VectorFilterExecutor *>(READ_LOCAL_ID());
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    OpVectorFilterExecuteInit(filter_exec, vpi);
    DISPATCH_NEXT();
  }

#define GEN_VEC_FILTER(BYTECODE)                                                      \
  OP(BYTECODE) : {                                                                    \
    auto *filter_exec = frame->LocalAt<sql::VectorFilterExecutor *>(READ_LOCAL_ID()); \
    auto left_col_idx = frame->LocalAt<uint32_t>(READ_LOCAL_ID());                    \
    auto right_col_idx = frame->LocalAt<uint32_t>(READ_LOCAL_ID());                   \
    Op##BYTECODE(filter_exec, left_col_idx, right_col_idx);                           \
    DISPATCH_NEXT();                                                                  \
  }                                                                                   \
  OP(BYTECODE##Val) : {                                                               \
    auto *filter_exec = frame->LocalAt<sql::VectorFilterExecutor *>(READ_LOCAL_ID()); \
    auto left_col_idx = frame->LocalAt<uint32_t>(READ_LOCAL_ID());                    \
    auto right_val = frame->LocalAt<sql::Val *>(READ_LOCAL_ID());                     \
    Op##BYTECODE##Val(filter_exec, left_col_idx, right_val);                          \
    DISPATCH_NEXT();                                                                  \
  }
  GEN_VEC_FILTER(VectorFilterExecuteEqual)
  GEN_VEC_FILTER(VectorFilterExecuteGreaterThan)
  GEN_VEC_FILTER(VectorFilterExecuteGreaterThanEqual)
  GEN_VEC_FILTER(VectorFilterExecuteLessThan)
  GEN_VEC_FILTER(VectorFilterExecuteLessThanEqual)
  GEN_VEC_FILTER(VectorFilterExecuteNotEqual)
#undef GEN_VEC_FILTER

  OP(VectorFilterExecuteFinish) : {
    auto *filter_exec = frame->LocalAt<sql::VectorFilterExecutor *>(READ_LOCAL_ID());
    OpVectorFilterExecuteFinish(filter_exec);
    DISPATCH_NEXT();
  }

  OP(VectorFilterExecuteFree) : {
    auto *filter_exec = frame->LocalAt<sql::VectorFilterExecutor *>(READ_LOCAL_ID());
    OpVectorFilterExecuteFree(filter_exec);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // SQL Integer Comparison Operations
  // -------------------------------------------------------

  OP(ForceBoolTruth) : {
    auto *result = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *sql_int = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());
    OpForceBoolTruth(result, sql_int);
    DISPATCH_NEXT();
  }

  OP(InitBool) : {
    auto *sql_bool = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());
    auto val = frame->LocalAt<bool>(READ_LOCAL_ID());
    OpInitBool(sql_bool, val);
    DISPATCH_NEXT();
  }

  OP(InitInteger) : {
    auto *sql_int = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    auto val = frame->LocalAt<int32_t>(READ_LOCAL_ID());
    OpInitInteger(sql_int, val);
    DISPATCH_NEXT();
  }

  OP(InitReal) : {
    auto *sql_real = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    auto val = frame->LocalAt<double>(READ_LOCAL_ID());
    OpInitReal(sql_real, val);
    DISPATCH_NEXT();
  }

  OP(InitDate) : {
    auto *sql_date = frame->LocalAt<sql::DateVal *>(READ_LOCAL_ID());
    auto year = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto month = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto day = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpInitDate(sql_date, year, month, day);
    DISPATCH_NEXT();
  }

#define GEN_CMP(op)                                                  \
  OP(op##Integer) : {                                                \
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());  \
    auto *left = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());    \
    auto *right = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());   \
    Op##op##Integer(result, left, right);                            \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(op##Real) : {                                                   \
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());  \
    auto *left = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());       \
    auto *right = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());      \
    Op##op##Real(result, left, right);                               \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(op##Date) : {                                                   \
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());  \
    auto *left = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());       \
    auto *right = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());      \
    Op##op##Real(result, left, right);                               \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(op##String) : {                                                 \
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());  \
    auto *left = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());  \
    auto *right = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID()); \
    Op##op##String(result, left, right);                             \
    DISPATCH_NEXT();                                                 \
  }
  GEN_CMP(GreaterThan);
  GEN_CMP(GreaterThanEqual);
  GEN_CMP(Equal);
  GEN_CMP(LessThan);
  GEN_CMP(LessThanEqual);
  GEN_CMP(NotEqual);
#undef GEN_CMP

#define GEN_UNARY_MATH_OPS(op)                                      \
  OP(op##Integer) : {                                               \
    auto *result = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID()); \
    auto *input = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());  \
    Op##op##Integer(result, input);                                 \
    DISPATCH_NEXT();                                                \
  }                                                                 \
  OP(op##Real) : {                                                  \
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());    \
    auto *input = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());     \
    Op##op##Real(result, input);                                    \
    DISPATCH_NEXT();                                                \
  }

  GEN_UNARY_MATH_OPS(Abs)

#undef GEN_UNARY_MATH_OPS

  OP(ValIsNull) : {
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<const sql::Val *>(READ_LOCAL_ID());
    OpValIsNull(result, val);
    DISPATCH_NEXT();
  }

  OP(ValIsNotNull) : {
    auto *result = frame->LocalAt<sql::BoolVal *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<const sql::Val *>(READ_LOCAL_ID());
    OpValIsNotNull(result, val);
    DISPATCH_NEXT();
  }

#define GEN_MATH_OPS(op)                                            \
  OP(op##Integer) : {                                               \
    auto *result = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID()); \
    auto *left = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());   \
    auto *right = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());  \
    Op##op##Integer(result, left, right);                           \
    DISPATCH_NEXT();                                                \
  }                                                                 \
  OP(op##Real) : {                                                  \
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());    \
    auto *left = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());      \
    auto *right = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());     \
    Op##op##Real(result, left, right);                              \
    DISPATCH_NEXT();                                                \
  }

  GEN_MATH_OPS(Add)
  GEN_MATH_OPS(Sub)
  GEN_MATH_OPS(Mul)
  GEN_MATH_OPS(Div)
  GEN_MATH_OPS(Rem)

#undef GEN_MATH_OPS

  // -------------------------------------------------------
  // Aggregations
  // -------------------------------------------------------

  OP(AggregationHashTableInit) : {
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto *memory = frame->LocalAt<tpl::sql::MemoryPool *>(READ_LOCAL_ID());
    auto payload_size = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpAggregationHashTableInit(agg_hash_table, memory, payload_size);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableInsert) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto hash = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    OpAggregationHashTableInsert(result, agg_hash_table, hash);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableInsertPartitioned) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto hash = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    OpAggregationHashTableInsertPartitioned(result, agg_hash_table, hash);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableLookup) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto hash = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    auto key_eq_fn_id = READ_FUNC_ID();
    auto *probe_tuple = frame->LocalAt<void *>(READ_LOCAL_ID());

    auto key_eq_fn = reinterpret_cast<sql::AggregationHashTable::KeyEqFn>(
        module_->GetRawFunctionImpl(key_eq_fn_id));
    OpAggregationHashTableLookup(result, agg_hash_table, hash, key_eq_fn, probe_tuple);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableProcessBatch) : {
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto hash_fn_id = READ_FUNC_ID();
    auto key_eq_fn_id = READ_FUNC_ID();
    auto init_agg_fn_id = READ_FUNC_ID();
    auto merge_agg_fn_id = READ_FUNC_ID();
    auto partitioned = frame->LocalAt<bool>(READ_LOCAL_ID());

    auto hash_fn = reinterpret_cast<sql::AggregationHashTable::HashFn>(
        module_->GetRawFunctionImpl(hash_fn_id));
    auto key_eq_fn = reinterpret_cast<sql::AggregationHashTable::KeyEqFn>(
        module_->GetRawFunctionImpl(key_eq_fn_id));
    auto init_agg_fn = reinterpret_cast<sql::AggregationHashTable::InitAggFn>(
        module_->GetRawFunctionImpl(init_agg_fn_id));
    auto advance_agg_fn = reinterpret_cast<sql::AggregationHashTable::AdvanceAggFn>(
        module_->GetRawFunctionImpl(merge_agg_fn_id));
    OpAggregationHashTableProcessBatch(agg_hash_table, vpi, hash_fn, key_eq_fn, init_agg_fn,
                                       advance_agg_fn, partitioned);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableTransferPartitions) : {
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto agg_ht_offset = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto merge_partition_fn_id = READ_FUNC_ID();

    auto merge_partition_fn = reinterpret_cast<sql::AggregationHashTable::MergePartitionFn>(
        module_->GetRawFunctionImpl(merge_partition_fn_id));
    OpAggregationHashTableTransferPartitions(agg_hash_table, thread_state_container, agg_ht_offset,
                                             merge_partition_fn);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableParallelPartitionedScan) : {
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    auto *query_state = frame->LocalAt<void *>(READ_LOCAL_ID());
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto scan_partition_fn_id = READ_FUNC_ID();

    auto scan_partition_fn = reinterpret_cast<sql::AggregationHashTable::ScanPartitionFn>(
        module_->GetRawFunctionImpl(scan_partition_fn_id));
    OpAggregationHashTableParallelPartitionedScan(agg_hash_table, query_state,
                                                  thread_state_container, scan_partition_fn);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableFree) : {
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    OpAggregationHashTableFree(agg_hash_table);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableIteratorInit) : {
    auto *iter = frame->LocalAt<sql::AHTIterator *>(READ_LOCAL_ID());
    auto *agg_hash_table = frame->LocalAt<sql::AggregationHashTable *>(READ_LOCAL_ID());
    OpAggregationHashTableIteratorInit(iter, agg_hash_table);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableIteratorHasNext) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::AHTIterator *>(READ_LOCAL_ID());
    OpAggregationHashTableIteratorHasNext(has_more, iter);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableIteratorNext) : {
    auto *agg_hash_table_iter = frame->LocalAt<sql::AHTIterator *>(READ_LOCAL_ID());
    OpAggregationHashTableIteratorNext(agg_hash_table_iter);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableIteratorGetRow) : {
    auto *row = frame->LocalAt<const byte **>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::AHTIterator *>(READ_LOCAL_ID());
    OpAggregationHashTableIteratorGetRow(row, iter);
    DISPATCH_NEXT();
  }

  OP(AggregationHashTableIteratorFree) : {
    auto *agg_hash_table_iter = frame->LocalAt<sql::AHTIterator *>(READ_LOCAL_ID());
    OpAggregationHashTableIteratorFree(agg_hash_table_iter);
    DISPATCH_NEXT();
  }

  OP(AggregationOverflowPartitionIteratorHasNext) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *overflow_iter = frame->LocalAt<sql::AHTOverflowPartitionIterator *>(READ_LOCAL_ID());
    OpAggregationOverflowPartitionIteratorHasNext(has_more, overflow_iter);
    DISPATCH_NEXT();
  }

  OP(AggregationOverflowPartitionIteratorNext) : {
    auto *overflow_iter = frame->LocalAt<sql::AHTOverflowPartitionIterator *>(READ_LOCAL_ID());
    OpAggregationOverflowPartitionIteratorNext(overflow_iter);
    DISPATCH_NEXT();
  }

  OP(AggregationOverflowPartitionIteratorGetHash) : {
    auto *hash = frame->LocalAt<hash_t *>(READ_LOCAL_ID());
    auto *overflow_iter = frame->LocalAt<sql::AHTOverflowPartitionIterator *>(READ_LOCAL_ID());
    OpAggregationOverflowPartitionIteratorGetHash(hash, overflow_iter);
    DISPATCH_NEXT();
  }

  OP(AggregationOverflowPartitionIteratorGetRow) : {
    auto *row = frame->LocalAt<const byte **>(READ_LOCAL_ID());
    auto *overflow_iter = frame->LocalAt<sql::AHTOverflowPartitionIterator *>(READ_LOCAL_ID());
    OpAggregationOverflowPartitionIteratorGetRow(row, overflow_iter);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Aggregates
  // -------------------------------------------------------

  OP(CountAggregateInit) : {
    auto *agg = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    OpCountAggregateInit(agg);
    DISPATCH_NEXT();
  }

  OP(CountAggregateAdvance) : {
    auto *agg = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<sql::Val *>(READ_LOCAL_ID());
    OpCountAggregateAdvance(agg, val);
    DISPATCH_NEXT();
  }

  OP(CountAggregateMerge) : {
    auto *agg_1 = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    auto *agg_2 = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    OpCountAggregateMerge(agg_1, agg_2);
    DISPATCH_NEXT();
  }

  OP(CountAggregateReset) : {
    auto *agg = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    OpCountAggregateReset(agg);
    DISPATCH_NEXT();
  }

  OP(CountAggregateGetResult) : {
    auto *result = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    auto *agg = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    OpCountAggregateGetResult(result, agg);
    DISPATCH_NEXT();
  }

  OP(CountAggregateFree) : {
    auto *agg = frame->LocalAt<sql::CountAggregate *>(READ_LOCAL_ID());
    OpCountAggregateFree(agg);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateInit) : {
    auto *agg = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    OpCountStarAggregateInit(agg);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateAdvance) : {
    auto *agg = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<sql::Val *>(READ_LOCAL_ID());
    OpCountStarAggregateAdvance(agg, val);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateMerge) : {
    auto *agg_1 = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    auto *agg_2 = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    OpCountStarAggregateMerge(agg_1, agg_2);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateReset) : {
    auto *agg = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    OpCountStarAggregateReset(agg);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateGetResult) : {
    auto *result = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    auto *agg = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    OpCountStarAggregateGetResult(result, agg);
    DISPATCH_NEXT();
  }

  OP(CountStarAggregateFree) : {
    auto *agg = frame->LocalAt<sql::CountStarAggregate *>(READ_LOCAL_ID());
    OpCountStarAggregateFree(agg);
    DISPATCH_NEXT();
  }

#define GEN_AGGREGATE(SQL_TYPE, AGG_TYPE)                            \
  OP(AGG_TYPE##Init) : {                                             \
    auto *agg = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());    \
    Op##AGG_TYPE##Init(agg);                                         \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(AGG_TYPE##Advance) : {                                          \
    auto *agg = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());    \
    auto *val = frame->LocalAt<sql::SQL_TYPE *>(READ_LOCAL_ID());    \
    Op##AGG_TYPE##Advance(agg, val);                                 \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(AGG_TYPE##Merge) : {                                            \
    auto *agg_1 = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());  \
    auto *agg_2 = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());  \
    Op##AGG_TYPE##Merge(agg_1, agg_2);                               \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(AGG_TYPE##Reset) : {                                            \
    auto *agg = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());    \
    Op##AGG_TYPE##Reset(agg);                                        \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(AGG_TYPE##GetResult) : {                                        \
    auto *result = frame->LocalAt<sql::SQL_TYPE *>(READ_LOCAL_ID()); \
    auto *agg = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());    \
    Op##AGG_TYPE##GetResult(result, agg);                            \
    DISPATCH_NEXT();                                                 \
  }                                                                  \
  OP(AGG_TYPE##Free) : {                                             \
    auto *agg = frame->LocalAt<sql::AGG_TYPE *>(READ_LOCAL_ID());    \
    Op##AGG_TYPE##Free(agg);                                         \
    DISPATCH_NEXT();                                                 \
  }

  GEN_AGGREGATE(Integer, IntegerSumAggregate);
  GEN_AGGREGATE(Integer, IntegerMaxAggregate);
  GEN_AGGREGATE(Integer, IntegerMinAggregate);
  GEN_AGGREGATE(Real, RealSumAggregate);
  GEN_AGGREGATE(Real, RealMaxAggregate);
  GEN_AGGREGATE(Real, RealMinAggregate);

#undef GEN_AGGREGATE

  OP(AvgAggregateInit) : {
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    OpAvgAggregateInit(agg);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateAdvanceInteger) : {
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    OpAvgAggregateAdvanceInteger(agg, val);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateAdvanceReal) : {
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    auto *val = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    OpAvgAggregateAdvanceReal(agg, val);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateMerge) : {
    auto *agg_1 = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    auto *agg_2 = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    OpAvgAggregateMerge(agg_1, agg_2);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateReset) : {
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    OpAvgAggregateReset(agg);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateGetResult) : {
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    OpAvgAggregateGetResult(result, agg);
    DISPATCH_NEXT();
  }

  OP(AvgAggregateFree) : {
    auto *agg = frame->LocalAt<sql::AvgAggregate *>(READ_LOCAL_ID());
    OpAvgAggregateFree(agg);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Hash Joins
  // -------------------------------------------------------

  OP(JoinHashTableInit) : {
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    auto *memory = frame->LocalAt<sql::MemoryPool *>(READ_LOCAL_ID());
    auto tuple_size = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpJoinHashTableInit(join_hash_table, memory, tuple_size);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableAllocTuple) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    auto hash = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    OpJoinHashTableAllocTuple(result, join_hash_table, hash);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableBuild) : {
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    OpJoinHashTableBuild(join_hash_table);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableBuildParallel) : {
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto jht_offset = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpJoinHashTableBuildParallel(join_hash_table, thread_state_container, jht_offset);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableLookup) : {
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    auto *ht_entry_iter = frame->LocalAt<sql::HashTableEntryIterator *>(READ_LOCAL_ID());
    auto hash_val = frame->LocalAt<hash_t>(READ_LOCAL_ID());
    OpJoinHashTableLookup(join_hash_table, ht_entry_iter, hash_val);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableFree) : {
    auto *join_hash_table = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    OpJoinHashTableFree(join_hash_table);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableVectorProbeInit) : {
    auto *jht_vector_probe = frame->LocalAt<sql::JoinHashTableVectorProbe *>(READ_LOCAL_ID());
    auto *jht = frame->LocalAt<sql::JoinHashTable *>(READ_LOCAL_ID());
    OpJoinHashTableVectorProbeInit(jht_vector_probe, jht);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableVectorProbePrepare) : {
    auto *jht_vector_probe = frame->LocalAt<sql::JoinHashTableVectorProbe *>(READ_LOCAL_ID());
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto hash_fn_id = READ_FUNC_ID();

    auto *hash_fn = reinterpret_cast<sql::JoinHashTableVectorProbe::HashFn>(
        module_->GetRawFunctionImpl(hash_fn_id));
    OpJoinHashTableVectorProbePrepare(jht_vector_probe, vpi, hash_fn);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableVectorProbeGetNextOutput) : {
    auto **result = frame->LocalAt<const byte **>(READ_LOCAL_ID());
    auto *jht_vector_probe = frame->LocalAt<sql::JoinHashTableVectorProbe *>(READ_LOCAL_ID());
    auto *vpi = frame->LocalAt<sql::VectorProjectionIterator *>(READ_LOCAL_ID());
    auto key_eq_fn_id = READ_FUNC_ID();

    auto *key_eq_fn = reinterpret_cast<sql::JoinHashTableVectorProbe::KeyEqFn>(
        module_->GetRawFunctionImpl(key_eq_fn_id));
    OpJoinHashTableVectorProbeGetNextOutput(result, jht_vector_probe, vpi, key_eq_fn);
    DISPATCH_NEXT();
  }

  OP(JoinHashTableVectorProbeFree) : {
    auto *jht_vector_probe = frame->LocalAt<sql::JoinHashTableVectorProbe *>(READ_LOCAL_ID());
    OpJoinHashTableVectorProbeFree(jht_vector_probe);
    DISPATCH_NEXT();
  }

  OP(HashTableEntryIteratorHasNext) : {
    auto *has_next = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *ht_entry_iter = frame->LocalAt<sql::HashTableEntryIterator *>(READ_LOCAL_ID());
    auto key_eq_fn_id = READ_FUNC_ID();
    auto *ctx = frame->LocalAt<void *>(READ_LOCAL_ID());
    auto *probe_tuple = frame->LocalAt<void *>(READ_LOCAL_ID());

    auto key_eq_fn = reinterpret_cast<sql::HashTableEntryIterator::KeyEq>(
        module_->GetRawFunctionImpl(key_eq_fn_id));
    OpHashTableEntryIteratorHasNext(has_next, ht_entry_iter, key_eq_fn, ctx, probe_tuple);
    DISPATCH_NEXT();
  }

  OP(HashTableEntryIteratorGetRow) : {
    const auto **row = frame->LocalAt<const byte **>(READ_LOCAL_ID());
    auto *ht_entry_iter = frame->LocalAt<sql::HashTableEntryIterator *>(READ_LOCAL_ID());
    OpHashTableEntryIteratorGetRow(row, ht_entry_iter);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Sorting
  // -------------------------------------------------------

  OP(SorterInit) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    auto *memory = frame->LocalAt<tpl::sql::MemoryPool *>(READ_LOCAL_ID());
    auto cmp_func_id = READ_FUNC_ID();
    auto tuple_size = frame->LocalAt<uint32_t>(READ_LOCAL_ID());

    auto cmp_fn =
        reinterpret_cast<sql::Sorter::ComparisonFunction>(module_->GetRawFunctionImpl(cmp_func_id));
    OpSorterInit(sorter, memory, cmp_fn, tuple_size);
    DISPATCH_NEXT();
  }

  OP(SorterAllocTuple) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    OpSorterAllocTuple(result, sorter);
    DISPATCH_NEXT();
  }

  OP(SorterAllocTupleTopK) : {
    auto *result = frame->LocalAt<byte **>(READ_LOCAL_ID());
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    auto top_k = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpSorterAllocTupleTopK(result, sorter, top_k);
    DISPATCH_NEXT();
  }

  OP(SorterAllocTupleTopKFinish) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    auto top_k = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpSorterAllocTupleTopKFinish(sorter, top_k);
    DISPATCH_NEXT();
  }

  OP(SorterSort) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    OpSorterSort(sorter);
    DISPATCH_NEXT();
  }

  OP(SorterSortParallel) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto sorter_offset = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    OpSorterSortParallel(sorter, thread_state_container, sorter_offset);
    DISPATCH_NEXT();
  }

  OP(SorterSortTopKParallel) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    auto *thread_state_container = frame->LocalAt<sql::ThreadStateContainer *>(READ_LOCAL_ID());
    auto sorter_offset = frame->LocalAt<uint32_t>(READ_LOCAL_ID());
    auto top_k = frame->LocalAt<uint64_t>(READ_LOCAL_ID());
    OpSorterSortTopKParallel(sorter, thread_state_container, sorter_offset, top_k);
    DISPATCH_NEXT();
  }

  OP(SorterFree) : {
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    OpSorterFree(sorter);
    DISPATCH_NEXT();
  }

  OP(SorterIteratorInit) : {
    auto *iter = frame->LocalAt<sql::SorterIterator *>(READ_LOCAL_ID());
    auto *sorter = frame->LocalAt<sql::Sorter *>(READ_LOCAL_ID());
    OpSorterIteratorInit(iter, sorter);
    DISPATCH_NEXT();
  }

  OP(SorterIteratorHasNext) : {
    auto *has_more = frame->LocalAt<bool *>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::SorterIterator *>(READ_LOCAL_ID());
    OpSorterIteratorHasNext(has_more, iter);
    DISPATCH_NEXT();
  }

  OP(SorterIteratorNext) : {
    auto *iter = frame->LocalAt<sql::SorterIterator *>(READ_LOCAL_ID());
    OpSorterIteratorNext(iter);
    DISPATCH_NEXT();
  }

  OP(SorterIteratorGetRow) : {
    const auto **row = frame->LocalAt<const byte **>(READ_LOCAL_ID());
    auto *iter = frame->LocalAt<sql::SorterIterator *>(READ_LOCAL_ID());
    OpSorterIteratorGetRow(row, iter);
    DISPATCH_NEXT();
  }

  OP(SorterIteratorFree) : {
    auto *iter = frame->LocalAt<sql::SorterIterator *>(READ_LOCAL_ID());
    OpSorterIteratorFree(iter);
    DISPATCH_NEXT();
  }

  // -------------------------------------------------------
  // Real-value functions
  // -------------------------------------------------------

  // -------------------------------------------------------
  // Trig functions
  // -------------------------------------------------------

  OP(Pi) : {
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    OpPi(result);
    DISPATCH_NEXT();
  }

  OP(E) : {
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    OpE(result);
    DISPATCH_NEXT();
  }

#define UNARY_REAL_MATH_OP(TOP)                                       \
  OP(TOP) : {                                                         \
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());      \
    auto *input = frame->LocalAt<const sql::Real *>(READ_LOCAL_ID()); \
    Op##TOP(result, input);                                           \
    DISPATCH_NEXT();                                                  \
  }

#define BINARY_REAL_MATH_OP(TOP)                                       \
  OP(TOP) : {                                                          \
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());       \
    auto *input1 = frame->LocalAt<const sql::Real *>(READ_LOCAL_ID()); \
    auto *input2 = frame->LocalAt<const sql::Real *>(READ_LOCAL_ID()); \
    Op##TOP(result, input1, input2);                                   \
    DISPATCH_NEXT();                                                   \
  }

  UNARY_REAL_MATH_OP(Sin);
  UNARY_REAL_MATH_OP(Asin);
  UNARY_REAL_MATH_OP(Cos);
  UNARY_REAL_MATH_OP(Acos);
  UNARY_REAL_MATH_OP(Tan);
  UNARY_REAL_MATH_OP(Cot);
  UNARY_REAL_MATH_OP(Atan);
  UNARY_REAL_MATH_OP(Cosh);
  UNARY_REAL_MATH_OP(Tanh);
  UNARY_REAL_MATH_OP(Sinh);
  UNARY_REAL_MATH_OP(Sqrt);
  UNARY_REAL_MATH_OP(Cbrt);
  UNARY_REAL_MATH_OP(Exp);
  UNARY_REAL_MATH_OP(Ceil);
  UNARY_REAL_MATH_OP(Floor);
  UNARY_REAL_MATH_OP(Truncate);
  UNARY_REAL_MATH_OP(Ln);
  UNARY_REAL_MATH_OP(Log2);
  UNARY_REAL_MATH_OP(Log10);
  UNARY_REAL_MATH_OP(Sign);
  UNARY_REAL_MATH_OP(Radians);
  UNARY_REAL_MATH_OP(Degrees);
  UNARY_REAL_MATH_OP(Round);
  BINARY_REAL_MATH_OP(Atan2);
  BINARY_REAL_MATH_OP(Log);
  BINARY_REAL_MATH_OP(Pow);

  OP(RoundUpTo) : {
    auto *result = frame->LocalAt<sql::Real *>(READ_LOCAL_ID());
    auto *v = frame->LocalAt<const sql::Real *>(READ_LOCAL_ID());
    auto *scale = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpRoundUpTo(result, v, scale);
    DISPATCH_NEXT();
  }

#undef BINARY_REAL_MATH_OP
#undef UNARY_REAL_MATH_OP

  // -------------------------------------------------------
  // String functions
  // -------------------------------------------------------

  OP(Left) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *n = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpLeft(exec_ctx, result, input, n);
    DISPATCH_NEXT();
  }

  OP(Length) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::Integer *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpLength(exec_ctx, result, input);
    DISPATCH_NEXT();
  }

  OP(Lower) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpLower(exec_ctx, result, input);
    DISPATCH_NEXT();
  }

  OP(LPad) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *n = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    auto *chars = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpLPad(exec_ctx, result, input, n, chars);
    DISPATCH_NEXT();
  }

  OP(LTrim) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *chars = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpLTrim(exec_ctx, result, input, chars);
    DISPATCH_NEXT();
  }

  OP(Repeat) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *n = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpRepeat(exec_ctx, result, input, n);
    DISPATCH_NEXT();
  }

  OP(Reverse) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpReverse(exec_ctx, result, input);
    DISPATCH_NEXT();
  }

  OP(Right) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *n = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpRight(exec_ctx, result, input, n);
    DISPATCH_NEXT();
  }

  OP(RPad) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *n = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    auto *chars = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpRPad(exec_ctx, result, input, n, chars);
    DISPATCH_NEXT();
  }

  OP(RTrim) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *input = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *chars = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpRTrim(exec_ctx, result, input, chars);
    DISPATCH_NEXT();
  }

  OP(SplitPart) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *str = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *delim = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *field = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpSplitPart(exec_ctx, result, str, delim, field);
    DISPATCH_NEXT();
  }

  OP(Substring) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *str = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *pos = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    auto *len = frame->LocalAt<const sql::Integer *>(READ_LOCAL_ID());
    OpSubstring(exec_ctx, result, str, pos, len);
    DISPATCH_NEXT();
  }

  OP(Trim) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *str = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    auto *chars = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpTrim(exec_ctx, result, str, chars);
    DISPATCH_NEXT();
  }

  OP(Upper) : {
    auto *exec_ctx = frame->LocalAt<sql::ExecutionContext *>(READ_LOCAL_ID());
    auto *result = frame->LocalAt<sql::StringVal *>(READ_LOCAL_ID());
    auto *str = frame->LocalAt<const sql::StringVal *>(READ_LOCAL_ID());
    OpUpper(exec_ctx, result, str);
    DISPATCH_NEXT();
  }

  // Impossible
  UNREACHABLE("Impossible to reach end of interpreter loop. Bad code!");
}

const uint8_t *VM::ExecuteCall(const uint8_t *ip, VM::Frame *caller) {
  // Read the function ID and the argument count to the function first
  const uint16_t func_id = READ_FUNC_ID();
  const uint16_t num_params = READ_UIMM2();

  // Lookup the function
  const FunctionInfo *func_info = module_->GetFuncInfoById(func_id);
  TPL_ASSERT(func_info != nullptr, "Function doesn't exist in module!");
  const std::size_t frame_size = func_info->frame_size();

  // Get some space for the function's frame
  bool used_heap = false;
  uint8_t *raw_frame = nullptr;
  if (frame_size > kMaxStackAllocSize) {
    used_heap = true;
    raw_frame = static_cast<uint8_t *>(util::MallocAligned(frame_size, alignof(uint64_t)));
  } else if (frame_size > kSoftMaxStackAllocSize) {
    // TODO(pmenon): Check stack before allocation
    raw_frame = static_cast<uint8_t *>(alloca(frame_size));
  } else {
    raw_frame = static_cast<uint8_t *>(alloca(frame_size));
  }

  // Set up the arguments to the function
  for (uint32_t i = 0; i < num_params; i++) {
    const LocalInfo &param_info = func_info->locals()[i];
    const LocalVar param = LocalVar::Decode(READ_LOCAL_ID());
    const void *param_ptr = caller->PtrToLocalAt(param);
    if (param.GetAddressMode() == LocalVar::AddressMode::Address) {
      std::memcpy(raw_frame + param_info.offset(), &param_ptr, param_info.size());
    } else {
      std::memcpy(raw_frame + param_info.offset(), param_ptr, param_info.size());
    }
  }

  LOG_DEBUG("Executing function '{}'", func_info->name());

  // Let's go
  const uint8_t *bytecode = module_->bytecode_module()->GetBytecodeForFunction(*func_info);
  TPL_ASSERT(bytecode != nullptr, "Bytecode cannot be null");
  VM::Frame callee(raw_frame, func_info->frame_size());
  Interpret(bytecode, &callee);

  if (used_heap) {
    std::free(raw_frame);
  }

  return ip;
}

}  // namespace tpl::vm
