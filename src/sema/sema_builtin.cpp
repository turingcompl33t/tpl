#include "sema/sema.h"

#include "ast/ast_node_factory.h"
#include "ast/context.h"
#include "ast/type.h"

namespace tpl::sema {

namespace {

bool IsPointerToSpecificBuiltin(ast::Type *type, ast::BuiltinType::Kind kind) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSpecificBuiltin(kind);
  }
  return false;
}

bool IsPointerToSQLValue(ast::Type *type) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSqlValueType();
  }
  return false;
}

bool IsPointerToAggregatorValue(ast::Type *type) {
  if (auto *pointee_type = type->GetPointeeType()) {
    return pointee_type->IsSqlAggregatorType();
  }
  return false;
}

template <typename... ArgTypes>
bool AreAllFunctions(const ArgTypes... type) {
  return (true && ... && type->IsFunctionType());
}

}  // namespace

void Sema::CheckSqlConversionCall(ast::CallExpr *call, ast::Builtin builtin) {
  // Handle this builtin because it's API is different than the other builtins; we expect three
  // 32-bit integer arguments.
  if (builtin == ast::Builtin::DateToSql) {
    if (!CheckArgCount(call, 3)) {
      return;
    }
    const auto int32_kind = ast::BuiltinType::Int32;
    if (!call->Arguments()[0]->GetType()->IsSpecificBuiltin(int32_kind) ||
        !call->Arguments()[1]->GetType()->IsSpecificBuiltin(int32_kind) ||
        !call->Arguments()[2]->GetType()->IsSpecificBuiltin(int32_kind)) {
      error_reporter()->Report(call->Position(), ErrorMessages::kInvalidCastToSqlDate,
                               call->Arguments()[0]->GetType(), call->Arguments()[1]->GetType(),
                               call->Arguments()[2]->GetType());
    }
    // All good. Set return type as SQL Date.
    call->SetType(GetBuiltinType(ast::BuiltinType::Date));
    return;
  }

  if (!CheckArgCount(call, 1)) {
    return;
  }

  auto input_type = call->Arguments()[0]->GetType();
  switch (builtin) {
    case ast::Builtin::BoolToSql: {
      if (!input_type->IsSpecificBuiltin(ast::BuiltinType::Bool)) {
        ReportIncorrectCallArg(call, 0, "boolean literal");
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Boolean));
      break;
    }
    case ast::Builtin::IntToSql: {
      if (!input_type->IsIntegerType()) {
        ReportIncorrectCallArg(call, 0, "integer literal");
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Integer));
      break;
    }
    case ast::Builtin::FloatToSql: {
      if (!input_type->IsFloatType()) {
        ReportIncorrectCallArg(call, 0, "floating point number literal");
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Real));
      break;
    }
    case ast::Builtin::StringToSql: {
      if (!input_type->IsStringType() || !call->Arguments()[0]->IsLiteralExpr()) {
        ReportIncorrectCallArg(call, 0, "string literal");
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::StringVal));
      break;
    }
    case ast::Builtin::SqlToBool: {
      if (!input_type->IsSpecificBuiltin(ast::BuiltinType::Boolean)) {
        error_reporter()->Report(call->Position(), ErrorMessages::kInvalidSqlCastToBool,
                                 input_type);
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }

#define CONVERSION_CASE(Op, InputType, OutputType)                     \
  case ast::Builtin::Op: {                                             \
    if (!input_type->IsSpecificBuiltin(ast::BuiltinType::InputType)) { \
      ReportIncorrectCallArg(call, 0, "SQL " #InputType);              \
      return;                                                          \
    }                                                                  \
    call->SetType(GetBuiltinType(ast::BuiltinType::OutputType));       \
    break;                                                             \
  }
      CONVERSION_CASE(ConvertBoolToInteger, Boolean, Integer);
      CONVERSION_CASE(ConvertIntegerToReal, Integer, Real);
      CONVERSION_CASE(ConvertDateToTimestamp, Date, Timestamp);
      CONVERSION_CASE(ConvertStringToBool, StringVal, Boolean);
      CONVERSION_CASE(ConvertStringToInt, StringVal, Integer);
      CONVERSION_CASE(ConvertStringToReal, StringVal, Real);
      CONVERSION_CASE(ConvertStringToDate, StringVal, Date);
      CONVERSION_CASE(ConvertStringToTime, StringVal, Timestamp);
#undef CONVERSION_CASE

    default: {
      UNREACHABLE("Impossible SQL conversion call");
    }
  }
}

void Sema::CheckNullValueCall(ast::CallExpr *call, UNUSED ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }
  // Input must be a SQL value.
  if (auto type = call->Arguments()[0]->GetType(); !type->IsSqlValueType()) {
    ErrorReporter().Report(call->Position(), ErrorMessages::kIsValNullExpectsSqlValue, type);
    return;
  }
  // Returns a primitive boolean.
  call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
}

void Sema::CheckBuiltinStringLikeCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // Both arguments must be SQL strings
  auto str_kind = ast::BuiltinType::StringVal;
  if (!call->Arguments()[0]->GetType()->IsSpecificBuiltin(str_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(str_kind));
    return;
  }
  if (!call->Arguments()[1]->GetType()->IsSpecificBuiltin(str_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(str_kind));
    return;
  }

  // Returns a SQL boolean
  call->SetType(GetBuiltinType(ast::BuiltinType::Boolean));
}

void Sema::CheckBuiltinDateFunctionCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }
  // First arg must be a date.
  auto date_kind = ast::BuiltinType::Date;
  if (!call->Arguments()[0]->GetType()->IsSpecificBuiltin(date_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(date_kind));
    return;
  }

  switch (builtin) {
    case ast::Builtin::ExtractYear:
      call->SetType(GetBuiltinType(ast::BuiltinType::Integer));
      return;
    default:
      UNREACHABLE("Impossible date function");
  }
}

void Sema::CheckBuiltinConcat(ast::CallExpr *call) {
  if (!CheckArgCountAtLeast(call, 3)) {
    return;
  }

  // First argument is an execution context.
  if (const auto ctx_kind = ast::BuiltinType::Kind::ExecutionContext;
      call->Arguments()[0]->GetType()->IsSpecificBuiltin(ctx_kind)) {
    return;
  }

  const auto string_val = ast::BuiltinType::Kind::StringVal;

  // All arguments must be SQL strings.
  for (unsigned i = 1; i < call->Arguments().size(); i++) {
    const auto arg = call->Arguments()[i];
    if (!arg->GetType()->IsSpecificBuiltin(ast::BuiltinType::Kind::StringVal)) {
      error_reporter()->Report(arg->Position(), ErrorMessages::kBadHashArg, arg->GetType());
      return;
    }
  }

  // Result is a string
  call->SetType(GetBuiltinType(string_val));
}

void Sema::CheckBuiltinAggHashTableCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->Arguments();

  const auto agg_ht_kind = ast::BuiltinType::AggregationHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), agg_ht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(agg_ht_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggHashTableInit: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument is a memory pool pointer
      const auto mem_pool_kind = ast::BuiltinType::MemoryPool;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), mem_pool_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(mem_pool_kind)->PointerTo());
        return;
      }
      // Third argument is the payload size, a 32-bit value
      const auto uint_kind = ast::BuiltinType::Uint32;
      if (!args[2]->GetType()->IsSpecificBuiltin(uint_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint_kind));
        return;
      }
      // Nil return
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableInsert: {
      if (!CheckArgCountAtLeast(call, 2)) {
        return;
      }
      // Second argument is the hash value
      const auto hash_val_kind = ast::BuiltinType::Uint64;
      if (!args[1]->GetType()->IsSpecificBuiltin(hash_val_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(hash_val_kind));
        return;
      }
      // If there's a third argument indicating regular or partitioned insertion, it must be a bool
      if (args.size() > 2 && (!args[2]->IsLiteralExpr() ||
                              !args[2]->GetType()->IsSpecificBuiltin(ast::BuiltinType::Bool))) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Bool));
        return;
      }
      // Return a byte pointer
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableLinkEntry: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // Second argument is a HashTableEntry*
      const auto entry_kind = ast::BuiltinType::HashTableEntry;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), entry_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(entry_kind)->PointerTo());
        return;
      }
      // Return nothing
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableLookup: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is the hash value
      const auto hash_val_kind = ast::BuiltinType::Uint64;
      if (!args[1]->GetType()->IsSpecificBuiltin(hash_val_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(hash_val_kind));
        return;
      }
      // Third argument is the key equality function
      if (!args[2]->GetType()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(hash_val_kind));
        return;
      }
      // Fourth argument is the probe tuple, but any pointer will do
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableProcessBatch: {
      if (!CheckArgCount(call, 6)) {
        return;
      }
      // Second argument is the input VPI.
      const auto vpi_kind = ast::BuiltinType::VectorProjectionIterator;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), vpi_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(vpi_kind)->PointerTo());
        return;
      }
      // Third argument is an array of key columns.
      if (auto array_type = args[2]->GetType()->SafeAs<ast::ArrayType>();
          array_type == nullptr || !array_type->HasKnownLength()) {
        ReportIncorrectCallArg(call, 2, "array with known length");
        return;
      }
      // Fourth and fifth argument is the initialization and advance functions.
      if (!AreAllFunctions(args[3]->GetType(), args[4]->GetType())) {
        ReportIncorrectCallArg(call, 3, "function");
        return;
      }
      // Last arg must be a boolean.
      if (!args[5]->GetType()->IsBoolType()) {
        ReportIncorrectCallArg(call, 5, GetBuiltinType(ast::BuiltinType::Bool));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableMovePartitions: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is the thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Third argument is the offset of the hash table in thread local state
      const auto uint32_kind = ast::BuiltinType::Uint32;
      if (!args[2]->GetType()->IsSpecificBuiltin(uint32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint32_kind));
        return;
      }
      // Fourth argument is the merging function
      if (!args[3]->GetType()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 3, GetBuiltinType(uint32_kind));
        return;
      }

      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableParallelPartitionedScan: {
      if (!CheckArgCount(call, 4)) {
        return;
      }
      // Second argument is an opaque context pointer
      if (!args[1]->GetType()->IsPointerType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(agg_ht_kind));
        return;
      }
      // Third argument is the thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(args[2]->GetType(), tls_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Fourth argument is the scanning function
      if (!args[3]->GetType()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 3, GetBuiltinType(tls_kind));
        return;
      }

      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableFree: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible aggregation hash table call");
    }
  }
}

void Sema::CheckBuiltinAggHashTableIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->Arguments();

  const auto agg_ht_iter_kind = ast::BuiltinType::AHTIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), agg_ht_iter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(agg_ht_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggHashTableIterInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      const auto agg_ht_kind = ast::BuiltinType::AggregationHashTable;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), agg_ht_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(agg_ht_kind)->PointerTo());
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableIterHasNext: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::AggHashTableIterNext: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggHashTableIterGetRow: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      const auto byte_kind = ast::BuiltinType::Uint8;
      call->SetType(GetBuiltinType(byte_kind)->PointerTo());
      break;
    }
    case ast::Builtin::AggHashTableIterClose: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible aggregation hash table iterator call");
    }
  }
}

void Sema::CheckBuiltinAggPartIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->Arguments();

  const auto part_iter_kind = ast::BuiltinType::AHTOverflowPartitionIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), part_iter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(part_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::AggPartIterHasNext: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::AggPartIterNext: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggPartIterGetRowEntry: {
      call->SetType(GetBuiltinType(ast::BuiltinType::HashTableEntry)->PointerTo());
      break;
    }
    case ast::Builtin::AggPartIterGetRow: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::AggPartIterGetHash: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint64));
      break;
    }
    default: {
      UNREACHABLE("Impossible aggregation partition iterator call");
    }
  }
}

void Sema::CheckBuiltinAggregatorCall(ast::CallExpr *call, ast::Builtin builtin) {
  const auto &args = call->Arguments();
  switch (builtin) {
    case ast::Builtin::AggInit:
    case ast::Builtin::AggReset: {
      // All arguments to @aggInit() or @aggReset() must be SQL aggregators
      for (uint32_t idx = 0; idx < call->NumArgs(); idx++) {
        if (!IsPointerToAggregatorValue(args[idx]->GetType())) {
          error_reporter()->Report(call->Position(), ErrorMessages::kNotASQLAggregate,
                                   args[idx]->GetType());
          return;
        }
      }
      // Init returns nil
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggAdvance: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // First argument to @aggAdvance() must be a SQL aggregator, second must be a SQL value
      if (!IsPointerToAggregatorValue(args[0]->GetType())) {
        error_reporter()->Report(call->Position(), ErrorMessages::kNotASQLAggregate,
                                 args[0]->GetType());
        return;
      }
      if (!IsPointerToSQLValue(args[1]->GetType())) {
        error_reporter()->Report(call->Position(), ErrorMessages::kNotASQLAggregate,
                                 args[1]->GetType());
        return;
      }
      // Advance returns nil
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggMerge: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // Both arguments must be SQL aggregators
      bool arg0_is_agg = IsPointerToAggregatorValue(args[0]->GetType());
      bool arg1_is_agg = IsPointerToAggregatorValue(args[1]->GetType());
      if (!arg0_is_agg || !arg1_is_agg) {
        error_reporter()->Report(call->Position(), ErrorMessages::kNotASQLAggregate,
                                 (!arg0_is_agg ? args[0]->GetType() : args[1]->GetType()));
        return;
      }
      // Merge returns nil
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::AggResult: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      // Argument must be a SQL aggregator
      if (!IsPointerToAggregatorValue(args[0]->GetType())) {
        error_reporter()->Report(call->Position(), ErrorMessages::kNotASQLAggregate,
                                 args[0]->GetType());
        return;
      }
      switch (args[0]->GetType()->GetPointeeType()->As<ast::BuiltinType>()->GetKind()) {
        case ast::BuiltinType::Kind::CountAggregate:
        case ast::BuiltinType::Kind::CountStarAggregate:
        case ast::BuiltinType::Kind::IntegerMaxAggregate:
        case ast::BuiltinType::Kind::IntegerMinAggregate:
        case ast::BuiltinType::Kind::IntegerSumAggregate:
          call->SetType(GetBuiltinType(ast::BuiltinType::Integer));
          break;
        case ast::BuiltinType::Kind::RealMaxAggregate:
        case ast::BuiltinType::Kind::RealMinAggregate:
        case ast::BuiltinType::Kind::RealSumAggregate:
        case ast::BuiltinType::Kind::AvgAggregate:
          call->SetType(GetBuiltinType(ast::BuiltinType::Real));
          break;
        default:
          UNREACHABLE("Impossible aggregate type!");
      }
      break;
    }
    default: {
      UNREACHABLE("Impossible aggregator call");
    }
  }
}

void Sema::CheckBuiltinJoinHashTableInit(ast::CallExpr *call) {
  if (!CheckArgCount(call, 3)) {
    return;
  }

  const auto &args = call->Arguments();

  // First argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Second argument must be a pointer to a MemoryPool
  const auto region_kind = ast::BuiltinType::MemoryPool;
  if (!IsPointerToSpecificBuiltin(args[1]->GetType(), region_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(region_kind)->PointerTo());
    return;
  }

  // Third and last argument must be a 32-bit number representing the tuple size
  if (!args[2]->GetType()->IsIntegerType()) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Uint32));
    return;
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableInsert(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  const auto &args = call->Arguments();

  // First argument is a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Second argument is a 64-bit unsigned hash value
  if (!args[1]->GetType()->IsSpecificBuiltin(ast::BuiltinType::Uint64)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint64));
    return;
  }

  // This call returns a byte pointer
  const auto byte_kind = ast::BuiltinType::Uint8;
  call->SetType(GetBuiltinType(byte_kind)->PointerTo());
}

void Sema::CheckBuiltinJoinHashTableBuild(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // The first and only argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::JoinHashTableBuild: {
      break;
    }
    case ast::Builtin::JoinHashTableBuildParallel: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument must be a thread state container pointer
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(call_args[1]->GetType(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }
      // Third argument must be a 32-bit integer representing the offset
      const auto uint32_kind = ast::BuiltinType::Uint32;
      if (!call_args[2]->GetType()->IsSpecificBuiltin(uint32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(uint32_kind));
        return;
      }
      break;
    }
    default: {
      UNREACHABLE("Impossible join hash table build call");
    }
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinJoinHashTableLookup(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // First argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // Second argument is a 64-bit unsigned hash value
  if (!call->Arguments()[1]->GetType()->IsSpecificBuiltin(ast::BuiltinType::Uint64)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint64));
    return;
  }

  call->SetType(GetBuiltinType(ast::BuiltinType::HashTableEntry)->PointerTo());
}

void Sema::CheckBuiltinJoinHashTableFree(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto &args = call->Arguments();

  // The first and only argument must be a pointer to a JoinHashTable
  const auto jht_kind = ast::BuiltinType::JoinHashTable;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), jht_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(jht_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinHashTableEntryCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // First argument must be the hash table entry iterator
  if (const auto entry_kind = ast::BuiltinType::HashTableEntry;
      !IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), entry_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(entry_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::HashTableEntryGetHash: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint64));
      break;
    }
    case ast::Builtin::HashTableEntryGetRow: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::HashTableEntryGetNext: {
      call->SetType(GetBuiltinType(ast::BuiltinType::HashTableEntry)->PointerTo());
      break;
    }
    default: {
      UNREACHABLE("Impossible hash table entry iterator call");
    }
  }
}

void Sema::CheckBuiltinExecutionContextCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // First and only argument should be the execution context
  auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::ExecutionContextGetMemoryPool: {
      call->SetType(GetBuiltinType(ast::BuiltinType::MemoryPool)->PointerTo());
      break;
    }
    case ast::Builtin::ExecutionContextGetTLS: {
      call->SetType(GetBuiltinType(ast::BuiltinType::ThreadStateContainer)->PointerTo());
      break;
    }
    default: {
      UNREACHABLE("Impossible execution context call");
    }
  }
}

void Sema::CheckBuiltinThreadStateContainerCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // First argument must be thread state container pointer
  auto tls_kind = ast::BuiltinType::ThreadStateContainer;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), tls_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(tls_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::ThreadStateContainerClear: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::ThreadStateContainerGetState: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::ThreadStateContainerReset: {
      if (!CheckArgCount(call, 5)) {
        return;
      }
      // Second argument must be an integer size of the state
      const auto uint_kind = ast::BuiltinType::Uint32;
      if (!call_args[1]->GetType()->IsSpecificBuiltin(uint_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(uint_kind));
        return;
      }
      // Third and fourth arguments must be functions
      // TODO(pmenon): More thorough check
      if (!AreAllFunctions(call_args[2]->GetType(), call_args[3]->GetType())) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      // Fifth argument must be a pointer to something or nil
      if (!call_args[4]->GetType()->IsPointerType() && !call_args[4]->GetType()->IsNilType()) {
        ReportIncorrectCallArg(call, 4, GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::ThreadStateContainerIterate: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument is a pointer to some context
      if (!call_args[1]->GetType()->IsPointerType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      // Third argument is the iteration function callback
      if (!call_args[2]->GetType()->IsFunctionType()) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(ast::BuiltinType::Uint32));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible table iteration call");
    }
  }
}

void Sema::CheckBuiltinTableIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  const auto &call_args = call->Arguments();

  const auto tvi_kind = ast::BuiltinType::TableVectorIterator;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), tvi_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(tvi_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::TableIterInit: {
      // The second argument is the table name as a literal string
      if (!call_args[1]->IsStringLiteral()) {
        ReportIncorrectCallArg(call, 1, ast::StringType::Get(context()));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::TableIterAdvance: {
      // A single-arg builtin returning a boolean
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::TableIterGetVPI: {
      // A single-arg builtin return a pointer to the current VPI
      const auto vpi_kind = ast::BuiltinType::VectorProjectionIterator;
      call->SetType(GetBuiltinType(vpi_kind)->PointerTo());
      break;
    }
    case ast::Builtin::TableIterClose: {
      // A single-arg builtin returning void
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible table iteration call");
    }
  }
}

void Sema::CheckBuiltinTableIterParCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // First argument is table name as a string literal
  if (!call_args[0]->IsStringLiteral()) {
    ReportIncorrectCallArg(call, 0, ast::StringType::Get(context()));
    return;
  }

  // Second argument is an opaque query state. For now, check it's a pointer.
  const auto void_kind = ast::BuiltinType::Nil;
  if (!call_args[1]->GetType()->IsPointerType()) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(void_kind)->PointerTo());
    return;
  }

  // Third argument is the thread state container
  const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
  if (!IsPointerToSpecificBuiltin(call_args[2]->GetType(), tls_kind)) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(tls_kind)->PointerTo());
    return;
  }

  // Third argument is scanner function
  auto *scan_fn_type = call_args[3]->GetType()->SafeAs<ast::FunctionType>();
  if (scan_fn_type == nullptr) {
    error_reporter()->Report(call->Position(), ErrorMessages::kBadParallelScanFunction,
                             call_args[3]->GetType());
    return;
  }
  // Check type
  const auto tvi_kind = ast::BuiltinType::TableVectorIterator;
  const auto &params = scan_fn_type->GetParams();
  if (params.size() != 3 || !params[0].type->IsPointerType() || !params[1].type->IsPointerType() ||
      !IsPointerToSpecificBuiltin(params[2].type, tvi_kind)) {
    error_reporter()->Report(call->Position(), ErrorMessages::kBadParallelScanFunction,
                             call_args[3]->GetType());
    return;
  }

  // Nil
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinVPICall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // The first argument must be a *VPI
  const auto vpi_kind = ast::BuiltinType::VectorProjectionIterator;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), vpi_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(vpi_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::VPIInit: {
      if (!CheckArgCountAtLeast(call, 2)) {
        return;
      }

      // The second argument must be a *VectorProjection
      const auto vp_kind = ast::BuiltinType::VectorProjection;
      if (!IsPointerToSpecificBuiltin(call_args[1]->GetType(), vp_kind)) {
        ReportIncorrectCallArg(call, 0, GetBuiltinType(vp_kind)->PointerTo());
        return;
      }

      // The third optional argument must be a *TupleIdList
      const auto tid_list_kind = ast::BuiltinType::TupleIdList;
      if (call_args.size() > 2 &&
          !IsPointerToSpecificBuiltin(call_args[2]->GetType(), tid_list_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(tid_list_kind)->PointerTo());
        return;
      }

      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::VPIFree: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::VPIIsFiltered:
    case ast::Builtin::VPIHasNext:
    case ast::Builtin::VPIAdvance:
    case ast::Builtin::VPIReset: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::VPIGetSelectedRowCount: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint32));
      break;
    }
    case ast::Builtin ::VPIGetVectorProjection: {
      call->SetType(GetBuiltinType(ast::BuiltinType::VectorProjection)->PointerTo());
      break;
    }
    case ast::Builtin::VPISetPosition: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      auto unsigned_kind = ast::BuiltinType::Uint32;
      if (!call_args[1]->GetType()->IsSpecificBuiltin(unsigned_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(unsigned_kind));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::VPIMatch: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      // If the match argument is a SQL boolean, implicitly cast to native
      ast::Expr *match_arg = call_args[1];
      if (match_arg->GetType()->IsSpecificBuiltin(ast::BuiltinType::Boolean)) {
        match_arg = ImplCastExprToType(match_arg, GetBuiltinType(ast::BuiltinType::Bool),
                                       ast::CastKind::SqlBoolToBool);
        call->SetArgument(1, match_arg);
      }
      // If the match argument isn't a native boolean , error
      if (!match_arg->GetType()->IsBoolType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Bool));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::VPIGetBool: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Boolean));
      break;
    }
    case ast::Builtin::VPIGetTinyInt:
    case ast::Builtin::VPIGetSmallInt:
    case ast::Builtin::VPIGetInt:
    case ast::Builtin::VPIGetBigInt: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Integer));
      break;
    }
    case ast::Builtin::VPIGetReal:
    case ast::Builtin::VPIGetDouble: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Real));
      break;
    }
    case ast::Builtin::VPIGetDate: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Date));
      break;
    }
    case ast::Builtin::VPIGetString: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::StringVal));
      break;
    }
    case ast::Builtin::VPIGetPointer: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::VPISetSmallInt:
    case ast::Builtin::VPISetInt:
    case ast::Builtin::VPISetBigInt:
    case ast::Builtin::VPISetReal:
    case ast::Builtin::VPISetDouble:
    case ast::Builtin::VPISetDate:
    case ast::Builtin::VPISetString: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument must be either an Integer or Real
      const auto sql_kind =
          (builtin == ast::Builtin::VPISetReal || builtin == ast::Builtin::VPISetDouble
               ? ast::BuiltinType::Real
           : builtin == ast::Builtin::VPISetDate   ? ast::BuiltinType::Date
           : builtin == ast::Builtin::VPISetString ? ast::BuiltinType::StringVal
                                                   : ast::BuiltinType::Integer);
      if (!call_args[1]->GetType()->IsSpecificBuiltin(sql_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(sql_kind));
        return;
      }
      // Third argument must be an integer
      const auto int32_kind = ast::BuiltinType::Int32;
      if (!call_args[2]->GetType()->IsSpecificBuiltin(int32_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(int32_kind));
        return;
      }
      break;
    }
    default: {
      UNREACHABLE("Impossible VPI call");
    }
  }
}

void Sema::CheckBuiltinCompactStorageWriteCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 4)) return;

  // First argument must be a pointer to where to store the value.
  if (!call->Arguments()[0]->GetType()->IsPointerType()) {
    ReportIncorrectCallArg(call, 0, "expected pointer to storage space.");
    return;
  }

  // Second argument must be a pointer to the NULL indicators array.
  if (!call->Arguments()[1]->GetType()->IsPointerType()) {
    ReportIncorrectCallArg(call, 0, "expected pointer to NULL indicators array.");
    return;
  }

  // Third argument is 4-byte index.
  if (!call->Arguments()[1]->GetType()->IsSpecificBuiltin(ast::BuiltinType::Int32)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Int32));
    return;
  }

  // Last argument is the SQL value.
  ast::BuiltinType::Kind expected_input_type;
  switch (builtin) {
    case ast::Builtin::CompactStorageWriteBool:
      expected_input_type = ast::BuiltinType::Boolean;
      break;
    case ast::Builtin::CompactStorageWriteTinyInt:
    case ast::Builtin::CompactStorageWriteSmallInt:
    case ast::Builtin::CompactStorageWriteInteger:
    case ast::Builtin::CompactStorageWriteBigInt:
      expected_input_type = ast::BuiltinType::Integer;
      break;
    case ast::Builtin::CompactStorageWriteReal:
    case ast::Builtin::CompactStorageWriteDouble:
      expected_input_type = ast::BuiltinType::Real;
      break;
    case ast::Builtin::CompactStorageWriteDate:
      expected_input_type = ast::BuiltinType::Date;
      break;
    case ast::Builtin::CompactStorageWriteTimestamp:
      expected_input_type = ast::BuiltinType::Timestamp;
      break;
    case ast::Builtin::CompactStorageWriteString:
      expected_input_type = ast::BuiltinType::StringVal;
      break;
    default: {
      UNREACHABLE("Impossible CompactStorage::Write() call!");
    }
  }

  // Last argument is the SQL value to write.
  if (!call->Arguments()[3]->GetType()->IsSpecificBuiltin(expected_input_type)) {
    ReportIncorrectCallArg(call, 3, GetBuiltinType(expected_input_type));
    return;
  }

  // No return.
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinCompactStorageReadCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 3)) return;

  // First argument must be a pointer to where the value is stored.
  if (!call->Arguments()[0]->GetType()->IsPointerType()) {
    ReportIncorrectCallArg(call, 0, "expected pointer to storage space.");
    return;
  }

  // Second argument must be a pointer to the NULL indicators array.
  if (!call->Arguments()[1]->GetType()->IsPointerType()) {
    ReportIncorrectCallArg(call, 0, "expected pointer to NULL indicators array.");
    return;
  }

  // Third argument is 4-byte index.
  if (!call->Arguments()[1]->GetType()->IsSpecificBuiltin(ast::BuiltinType::Int32)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Int32));
    return;
  }

  // Set return type.
  ast::BuiltinType::Kind return_type;
  switch (builtin) {
    case ast::Builtin::CompactStorageReadBool:
      return_type = ast::BuiltinType::Boolean;
      break;
    case ast::Builtin::CompactStorageReadTinyInt:
    case ast::Builtin::CompactStorageReadSmallInt:
    case ast::Builtin::CompactStorageReadInteger:
    case ast::Builtin::CompactStorageReadBigInt:
      return_type = ast::BuiltinType::Integer;
      break;
    case ast::Builtin::CompactStorageReadReal:
    case ast::Builtin::CompactStorageReadDouble:
      return_type = ast::BuiltinType::Real;
      break;
    case ast::Builtin::CompactStorageReadDate:
      return_type = ast::BuiltinType::Date;
      break;
    case ast::Builtin::CompactStorageReadTimestamp:
      return_type = ast::BuiltinType::Timestamp;
      break;
    case ast::Builtin::CompactStorageReadString:
      return_type = ast::BuiltinType::StringVal;
      break;
    default: {
      UNREACHABLE("Impossible CompactStorage::Read() call!");
    }
  }

  call->SetType(GetBuiltinType(return_type));
}

void Sema::CheckBuiltinHashCall(ast::CallExpr *call, UNUSED ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // All arguments must be SQL types
  for (const auto &arg : call->Arguments()) {
    if (!arg->GetType()->IsSqlValueType()) {
      error_reporter()->Report(arg->Position(), ErrorMessages::kBadHashArg, arg->GetType());
      return;
    }
  }

  // Result is a hash value
  call->SetType(GetBuiltinType(ast::BuiltinType::Uint64));
}

void Sema::CheckBuiltinFilterManagerCall(ast::CallExpr *const call, const ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // The first argument must be a *FilterManagerBuilder
  const auto fm_kind = ast::BuiltinType::FilterManager;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), fm_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(fm_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::FilterManagerInit:
    case ast::Builtin::FilterManagerFree: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::FilterManagerInsertFilter: {
      for (uint32_t arg_idx = 1; arg_idx < call->NumArgs(); arg_idx++) {
        const auto vector_proj_kind = ast::BuiltinType::VectorProjection;
        const auto tid_list_kind = ast::BuiltinType::TupleIdList;
        auto *arg_type = call->Arguments()[arg_idx]->GetType()->SafeAs<ast::FunctionType>();
        if (arg_type == nullptr || arg_type->GetNumParams() != 3 ||
            !IsPointerToSpecificBuiltin(arg_type->GetParams()[0].type, vector_proj_kind) ||
            !IsPointerToSpecificBuiltin(arg_type->GetParams()[1].type, tid_list_kind) ||
            !arg_type->GetParams()[2].type->IsPointerType()) {
          ReportIncorrectCallArg(call, arg_idx, "(*VectorProjection, *TupleIdList, *uint8)->nil");
          return;
        }
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::FilterManagerRunFilters: {
      if (!CheckArgCount(call, 2)) {
        return;
      }

      const auto vpi_kind = ast::BuiltinType::VectorProjectionIterator;
      if (!IsPointerToSpecificBuiltin(call->Arguments()[1]->GetType(), vpi_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(vpi_kind)->PointerTo());
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible FilterManager call");
    }
  }
}

void Sema::CheckBuiltinVectorFilterCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  // The first argument must be a *VectorProjection
  const auto vector_proj_kind = ast::BuiltinType::VectorProjection;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), vector_proj_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(vector_proj_kind)->PointerTo());
    return;
  }

  // Second argument is the column index
  const auto &call_args = call->Arguments();
  const auto int32_kind = ast::BuiltinType::Int32;
  const auto uint32_kind = ast::BuiltinType::Uint32;
  if (!call_args[1]->GetType()->IsSpecificBuiltin(int32_kind) &&
      !call_args[1]->GetType()->IsSpecificBuiltin(uint32_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(int32_kind));
    return;
  }

  // Third argument is either an integer or a pointer to a generic value
  if (!call_args[2]->GetType()->IsSpecificBuiltin(int32_kind) &&
      !call_args[2]->GetType()->IsSqlValueType()) {
    ReportIncorrectCallArg(call, 2, GetBuiltinType(int32_kind));
    return;
  }

  // Fourth and last argument is the *TupleIdList
  const auto tid_list_kind = ast::BuiltinType::TupleIdList;
  if (!IsPointerToSpecificBuiltin(call_args[3]->GetType(), tid_list_kind)) {
    ReportIncorrectCallArg(call, 3, GetBuiltinType(tid_list_kind)->PointerTo());
    return;
  }

  // Done
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckMathTrigCall(ast::CallExpr *call, ast::Builtin builtin) {
  const auto real_kind = ast::BuiltinType::Real;

  const auto &call_args = call->Arguments();
  switch (builtin) {
    case ast::Builtin::ATan2: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      if (!call_args[0]->GetType()->IsSpecificBuiltin(real_kind) ||
          !call_args[1]->GetType()->IsSpecificBuiltin(real_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(real_kind));
        return;
      }
      break;
    }
    case ast::Builtin::Cos:
    case ast::Builtin::Cot:
    case ast::Builtin::Sin:
    case ast::Builtin::Tan:
    case ast::Builtin::ACos:
    case ast::Builtin::ASin:
    case ast::Builtin::ATan: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      if (!call_args[0]->GetType()->IsSpecificBuiltin(real_kind)) {
        ReportIncorrectCallArg(call, 0, GetBuiltinType(real_kind));
        return;
      }
      break;
    }
    default: {
      UNREACHABLE("Impossible math trig function call");
    }
  }

  // Trig functions return real values
  call->SetType(GetBuiltinType(real_kind));
}

void Sema::CheckResultBufferCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  const auto exec_ctx_kind = ast::BuiltinType::ExecutionContext;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), exec_ctx_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(exec_ctx_kind)->PointerTo());
    return;
  }

  if (builtin == ast::Builtin::ResultBufferAllocOutRow) {
    call->SetType(ast::BuiltinType::Get(context(), ast::BuiltinType::Uint8)->PointerTo());
  } else {
    call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
  }
}

void Sema::CheckCSVReaderCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // First argument must be a *CSVReader.
  const auto csv_reader = ast::BuiltinType::CSVReader;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), csv_reader)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(csv_reader));
    return;
  }

  switch (builtin) {
    case ast::Builtin::CSVReaderInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }

      // Second argument is either a raw string, or a string representing the
      // name of the CSV file to read. At this stage, we don't care. It just
      // needs to be a string.
      if (!call_args[1]->GetType()->IsStringType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(csv_reader));
        return;
      }

      // Third, fourth, and fifth must be characters.

      // Returns boolean indicating if initialization succeeded.
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::CSVReaderAdvance: {
      // Returns a boolean indicating if there's more data.
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::CSVReaderGetField: {
      if (!CheckArgCount(call, 3)) {
        return;
      }
      // Second argument must be the index, third is a pointer to a SQL string.
      if (!call_args[1]->GetType()->IsIntegerType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(ast::BuiltinType::Uint32));
      }
      // Second argument must be the index, third is a pointer to a SQL string.
      const auto string_kind = ast::BuiltinType::StringVal;
      if (!IsPointerToSpecificBuiltin(call_args[2]->GetType(), string_kind)) {
        ReportIncorrectCallArg(call, 2, GetBuiltinType(string_kind)->PointerTo());
      }
      // Returns nothing.
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::CSVReaderGetRecordNumber: {
      // Returns a 32-bit number indicating the current record number.
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint32));
      break;
    }
    case ast::Builtin::CSVReaderClose: {
      // Returns nothing.
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default:
      UNREACHABLE("Impossible math trig function call");
  }
}

void Sema::CheckBuiltinSizeOfCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // This call returns an unsigned 32-bit value for the size of the type
  call->SetType(GetBuiltinType(ast::BuiltinType::Uint32));
}

void Sema::CheckBuiltinOffsetOfCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // First argument must be a resolved composite type
  auto *type = Resolve(call->Arguments()[0]);
  if (type == nullptr || !type->IsStructType()) {
    ReportIncorrectCallArg(call, 0, "composite");
    return;
  }

  // Second argument must be an identifier expression
  auto field = call->Arguments()[1]->SafeAs<ast::IdentifierExpr>();
  if (field == nullptr) {
    ReportIncorrectCallArg(call, 1, "identifier expression");
    return;
  }

  // Field with the given name must exist in the composite type
  if (type->As<ast::StructType>()->LookupFieldByName(field->Name()) == nullptr) {
    error_reporter()->Report(call->Position(), ErrorMessages::kFieldObjectDoesNotExist,
                             field->Name(), type);
    return;
  }

  // Returns a 32-bit value for the offset of the type
  call->SetType(GetBuiltinType(ast::BuiltinType::Uint32));
}

void Sema::CheckBuiltinPtrCastCall(ast::CallExpr *call) {
  if (!CheckArgCount(call, 2)) {
    return;
  }

  // The first argument will be a UnaryOpExpr with the '*' (star) op. This is
  // because parsing function calls assumes expression arguments, not types. So,
  // something like '*Type', which would be the first argument to @ptrCast, will
  // get parsed as a dereference expression before a type expression.
  // TODO(pmenon): Fix the above to parse correctly

  auto unary_op = call->Arguments()[0]->SafeAs<ast::UnaryOpExpr>();
  if (unary_op == nullptr || unary_op->Op() != parsing::Token::Type::STAR) {
    error_reporter()->Report(call->Position(), ErrorMessages::kBadArgToPtrCast,
                             call->Arguments()[0]->GetType(), 1);
    return;
  }

  // Replace the unary with a PointerTypeRepr node and resolve it
  call->SetArgument(0, context()->GetNodeFactory()->NewPointerType(call->Arguments()[0]->Position(),
                                                                   unary_op->Input()));

  for (auto *arg : call->Arguments()) {
    auto *resolved_type = Resolve(arg);
    if (resolved_type == nullptr) {
      return;
    }
  }

  // Both arguments must be pointer types
  if (!call->Arguments()[0]->GetType()->IsPointerType() ||
      !call->Arguments()[1]->GetType()->IsPointerType()) {
    error_reporter()->Report(call->Position(), ErrorMessages::kBadArgToPtrCast,
                             call->Arguments()[0]->GetType(), 1);
    return;
  }

  // Apply the cast
  call->SetType(call->Arguments()[0]->GetType());
}

void Sema::CheckBuiltinSorterInit(ast::CallExpr *call) {
  if (!CheckArgCount(call, 4)) {
    return;
  }

  const auto &args = call->Arguments();

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // Second argument must be a pointer to a MemoryPool
  const auto mem_kind = ast::BuiltinType::MemoryPool;
  if (!IsPointerToSpecificBuiltin(args[1]->GetType(), mem_kind)) {
    ReportIncorrectCallArg(call, 1, GetBuiltinType(mem_kind)->PointerTo());
    return;
  }

  // Second argument must be a function
  auto *const cmp_func_type = args[2]->GetType()->SafeAs<ast::FunctionType>();
  if (cmp_func_type == nullptr || cmp_func_type->GetNumParams() != 2 ||
      !cmp_func_type->GetReturnType()->IsSpecificBuiltin(ast::BuiltinType::Bool) ||
      !cmp_func_type->GetParams()[0].type->IsPointerType() ||
      !cmp_func_type->GetParams()[1].type->IsPointerType()) {
    error_reporter()->Report(call->Position(), ErrorMessages::kBadComparisonFunctionForSorter,
                             args[2]->GetType());
    return;
  }

  // Third and last argument must be a 32-bit number representing the tuple size
  const auto uint_kind = ast::BuiltinType::Uint32;
  if (!args[3]->GetType()->IsSpecificBuiltin(uint_kind)) {
    ReportIncorrectCallArg(call, 3, GetBuiltinType(uint_kind));
    return;
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterInsert(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // If it's an insertion for Top-K, the second argument must be an unsigned integer.
  if (builtin == ast::Builtin::SorterInsertTopK ||
      builtin == ast::Builtin::SorterInsertTopKFinish) {
    if (!CheckArgCount(call, 2)) {
      return;
    }

    // Error if the top-k argument isn't an integer
    ast::Type *uint_type = GetBuiltinType(ast::BuiltinType::Uint32);
    if (!call->Arguments()[1]->GetType()->IsIntegerType()) {
      ReportIncorrectCallArg(call, 1, uint_type);
      return;
    }
    if (call->Arguments()[1]->GetType() != uint_type) {
      call->SetArgument(
          1, ImplCastExprToType(call->Arguments()[1], uint_type, ast::CastKind::IntegralCast));
    }
  } else {
    // Regular sorter insert, expect one argument.
    if (!CheckArgCount(call, 1)) {
      return;
    }
  }

  // This call returns a pointer to the allocated tuple
  call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
}

void Sema::CheckBuiltinSorterSort(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &call_args = call->Arguments();

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call_args[0]->GetType(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::SorterSort: {
      if (!CheckArgCount(call, 1)) {
        return;
      }
      break;
    }
    case ast::Builtin::SorterSortParallel:
    case ast::Builtin::SorterSortTopKParallel: {
      // Second argument is the *ThreadStateContainer.
      const auto tls_kind = ast::BuiltinType::ThreadStateContainer;
      if (!IsPointerToSpecificBuiltin(call_args[1]->GetType(), tls_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(tls_kind)->PointerTo());
        return;
      }

      // Third argument must be a 32-bit integer representing the offset.
      ast::Type *uint_type = GetBuiltinType(ast::BuiltinType::Uint32);
      if (call_args[2]->GetType() != uint_type) {
        ReportIncorrectCallArg(call, 2, uint_type);
        return;
      }

      // If it's for top-k, the last argument must be the top-k value
      if (builtin == ast::Builtin::SorterSortParallel) {
        if (!CheckArgCount(call, 3)) {
          return;
        }
      } else {
        if (!CheckArgCount(call, 4)) {
          return;
        }
        if (!call_args[3]->GetType()->IsIntegerType()) {
          ReportIncorrectCallArg(call, 3, uint_type);
          return;
        }
        if (call_args[3]->GetType() != uint_type) {
          call->SetArgument(
              3, ImplCastExprToType(call_args[3], uint_type, ast::CastKind::IntegralCast));
        }
      }
      break;
    }
    default: {
      UNREACHABLE("Impossible sorter sort call");
    }
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterFree(ast::CallExpr *call) {
  if (!CheckArgCount(call, 1)) {
    return;
  }

  // First argument must be a pointer to a Sorter
  const auto sorter_kind = ast::BuiltinType::Sorter;
  if (!IsPointerToSpecificBuiltin(call->Arguments()[0]->GetType(), sorter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_kind)->PointerTo());
    return;
  }

  // This call returns nothing
  call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
}

void Sema::CheckBuiltinSorterIterCall(ast::CallExpr *call, ast::Builtin builtin) {
  if (!CheckArgCountAtLeast(call, 1)) {
    return;
  }

  const auto &args = call->Arguments();

  const auto sorter_iter_kind = ast::BuiltinType::SorterIterator;
  if (!IsPointerToSpecificBuiltin(args[0]->GetType(), sorter_iter_kind)) {
    ReportIncorrectCallArg(call, 0, GetBuiltinType(sorter_iter_kind)->PointerTo());
    return;
  }

  switch (builtin) {
    case ast::Builtin::SorterIterInit: {
      if (!CheckArgCount(call, 2)) {
        return;
      }

      // The second argument is the sorter instance to iterate over
      const auto sorter_kind = ast::BuiltinType::Sorter;
      if (!IsPointerToSpecificBuiltin(args[1]->GetType(), sorter_kind)) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(sorter_kind)->PointerTo());
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::SorterIterHasNext: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Bool));
      break;
    }
    case ast::Builtin::SorterIterNext: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::SorterIterSkipRows: {
      if (!CheckArgCount(call, 2)) {
        return;
      }
      const auto uint_kind = ast::BuiltinType::Kind::Uint32;
      if (!args[1]->GetType()->IsIntegerType()) {
        ReportIncorrectCallArg(call, 1, GetBuiltinType(uint_kind));
        return;
      }
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    case ast::Builtin::SorterIterGetRow: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Uint8)->PointerTo());
      break;
    }
    case ast::Builtin::SorterIterClose: {
      call->SetType(GetBuiltinType(ast::BuiltinType::Nil));
      break;
    }
    default: {
      UNREACHABLE("Impossible table iteration call");
    }
  }
}

void Sema::CheckBuiltinCall(ast::CallExpr *call) {
  ast::Builtin builtin;
  if (!context()->IsBuiltinFunction(call->GetFuncName(), &builtin)) {
    error_reporter()->Report(call->Function()->Position(), ErrorMessages::kInvalidBuiltinFunction,
                             call->GetFuncName());
    return;
  }

  if (builtin == ast::Builtin::PtrCast) {
    CheckBuiltinPtrCastCall(call);
    return;
  }

  if (builtin == ast::Builtin::OffsetOf) {
    CheckBuiltinOffsetOfCall(call);
    return;
  }

  // First, resolve all call arguments. If any fail, exit immediately.
  for (auto *arg : call->Arguments()) {
    auto *resolved_type = Resolve(arg);
    if (resolved_type == nullptr) {
      return;
    }
  }

  switch (builtin) {
    case ast::Builtin::BoolToSql:
    case ast::Builtin::IntToSql:
    case ast::Builtin::FloatToSql:
    case ast::Builtin::DateToSql:
    case ast::Builtin::StringToSql:
    case ast::Builtin::SqlToBool:
    case ast::Builtin::ConvertBoolToInteger:
    case ast::Builtin::ConvertIntegerToReal:
    case ast::Builtin::ConvertDateToTimestamp:
    case ast::Builtin::ConvertStringToBool:
    case ast::Builtin::ConvertStringToInt:
    case ast::Builtin::ConvertStringToReal:
    case ast::Builtin::ConvertStringToDate:
    case ast::Builtin::ConvertStringToTime: {
      CheckSqlConversionCall(call, builtin);
      break;
    }
    case ast::Builtin::IsValNull: {
      CheckNullValueCall(call, builtin);
      break;
    }
    case ast::Builtin::Like: {
      CheckBuiltinStringLikeCall(call);
      break;
    }
    case ast::Builtin::ExtractYear: {
      CheckBuiltinDateFunctionCall(call, builtin);
      break;
    }
    case ast::Builtin::Concat: {
      CheckBuiltinConcat(call);
      break;
    }
    case ast::Builtin::ExecutionContextGetMemoryPool:
    case ast::Builtin::ExecutionContextGetTLS: {
      CheckBuiltinExecutionContextCall(call, builtin);
      break;
    }
    case ast::Builtin::ThreadStateContainerReset:
    case ast::Builtin::ThreadStateContainerGetState:
    case ast::Builtin::ThreadStateContainerIterate:
    case ast::Builtin::ThreadStateContainerClear: {
      CheckBuiltinThreadStateContainerCall(call, builtin);
      break;
    }
    case ast::Builtin::TableIterInit:
    case ast::Builtin::TableIterAdvance:
    case ast::Builtin::TableIterGetVPI:
    case ast::Builtin::TableIterClose: {
      CheckBuiltinTableIterCall(call, builtin);
      break;
    }
    case ast::Builtin::TableIterParallel: {
      CheckBuiltinTableIterParCall(call);
      break;
    }
    case ast::Builtin::VPIInit:
    case ast::Builtin::VPIFree:
    case ast::Builtin::VPIIsFiltered:
    case ast::Builtin::VPIGetSelectedRowCount:
    case ast::Builtin::VPIGetVectorProjection:
    case ast::Builtin::VPIHasNext:
    case ast::Builtin::VPIAdvance:
    case ast::Builtin::VPISetPosition:
    case ast::Builtin::VPIMatch:
    case ast::Builtin::VPIReset:
    case ast::Builtin::VPIGetBool:
    case ast::Builtin::VPIGetTinyInt:
    case ast::Builtin::VPIGetSmallInt:
    case ast::Builtin::VPIGetInt:
    case ast::Builtin::VPIGetBigInt:
    case ast::Builtin::VPIGetReal:
    case ast::Builtin::VPIGetDouble:
    case ast::Builtin::VPIGetDate:
    case ast::Builtin::VPIGetString:
    case ast::Builtin::VPIGetPointer:
    case ast::Builtin::VPISetBool:
    case ast::Builtin::VPISetTinyInt:
    case ast::Builtin::VPISetSmallInt:
    case ast::Builtin::VPISetInt:
    case ast::Builtin::VPISetBigInt:
    case ast::Builtin::VPISetReal:
    case ast::Builtin::VPISetDouble:
    case ast::Builtin::VPISetDate:
    case ast::Builtin::VPISetString: {
      CheckBuiltinVPICall(call, builtin);
      break;
    }
    case ast::Builtin::CompactStorageWriteBool:
    case ast::Builtin::CompactStorageWriteTinyInt:
    case ast::Builtin::CompactStorageWriteSmallInt:
    case ast::Builtin::CompactStorageWriteInteger:
    case ast::Builtin::CompactStorageWriteBigInt:
    case ast::Builtin::CompactStorageWriteReal:
    case ast::Builtin::CompactStorageWriteDouble:
    case ast::Builtin::CompactStorageWriteDate:
    case ast::Builtin::CompactStorageWriteTimestamp:
    case ast::Builtin::CompactStorageWriteString: {
      CheckBuiltinCompactStorageWriteCall(call, builtin);
      break;
    }
    case ast::Builtin::CompactStorageReadBool:
    case ast::Builtin::CompactStorageReadTinyInt:
    case ast::Builtin::CompactStorageReadSmallInt:
    case ast::Builtin::CompactStorageReadInteger:
    case ast::Builtin::CompactStorageReadBigInt:
    case ast::Builtin::CompactStorageReadReal:
    case ast::Builtin::CompactStorageReadDouble:
    case ast::Builtin::CompactStorageReadDate:
    case ast::Builtin::CompactStorageReadTimestamp:
    case ast::Builtin::CompactStorageReadString: {
      CheckBuiltinCompactStorageReadCall(call, builtin);
      break;
    }
    case ast::Builtin::Hash: {
      CheckBuiltinHashCall(call, builtin);
      break;
    }
    case ast::Builtin::FilterManagerInit:
    case ast::Builtin::FilterManagerInsertFilter:
    case ast::Builtin::FilterManagerRunFilters:
    case ast::Builtin::FilterManagerFree: {
      CheckBuiltinFilterManagerCall(call, builtin);
      break;
    }
    case ast::Builtin::VectorFilterEqual:
    case ast::Builtin::VectorFilterGreaterThan:
    case ast::Builtin::VectorFilterGreaterThanEqual:
    case ast::Builtin::VectorFilterLessThan:
    case ast::Builtin::VectorFilterLessThanEqual:
    case ast::Builtin::VectorFilterNotEqual: {
      CheckBuiltinVectorFilterCall(call);
      break;
    }
    case ast::Builtin::AggHashTableInit:
    case ast::Builtin::AggHashTableInsert:
    case ast::Builtin::AggHashTableLinkEntry:
    case ast::Builtin::AggHashTableLookup:
    case ast::Builtin::AggHashTableProcessBatch:
    case ast::Builtin::AggHashTableMovePartitions:
    case ast::Builtin::AggHashTableParallelPartitionedScan:
    case ast::Builtin::AggHashTableFree: {
      CheckBuiltinAggHashTableCall(call, builtin);
      break;
    }
    case ast::Builtin::AggHashTableIterInit:
    case ast::Builtin::AggHashTableIterHasNext:
    case ast::Builtin::AggHashTableIterNext:
    case ast::Builtin::AggHashTableIterGetRow:
    case ast::Builtin::AggHashTableIterClose: {
      CheckBuiltinAggHashTableIterCall(call, builtin);
      break;
    }
    case ast::Builtin::AggPartIterHasNext:
    case ast::Builtin::AggPartIterNext:
    case ast::Builtin::AggPartIterGetRow:
    case ast::Builtin::AggPartIterGetRowEntry:
    case ast::Builtin::AggPartIterGetHash: {
      CheckBuiltinAggPartIterCall(call, builtin);
      break;
    }
    case ast::Builtin::AggInit:
    case ast::Builtin::AggAdvance:
    case ast::Builtin::AggMerge:
    case ast::Builtin::AggReset:
    case ast::Builtin::AggResult: {
      CheckBuiltinAggregatorCall(call, builtin);
      break;
    }
    case ast::Builtin::JoinHashTableInit: {
      CheckBuiltinJoinHashTableInit(call);
      break;
    }
    case ast::Builtin::JoinHashTableInsert: {
      CheckBuiltinJoinHashTableInsert(call);
      break;
    }
    case ast::Builtin::JoinHashTableBuild:
    case ast::Builtin::JoinHashTableBuildParallel: {
      CheckBuiltinJoinHashTableBuild(call, builtin);
      break;
    }
    case ast::Builtin::JoinHashTableLookup: {
      CheckBuiltinJoinHashTableLookup(call);
      break;
    }
    case ast::Builtin::JoinHashTableFree: {
      CheckBuiltinJoinHashTableFree(call);
      break;
    }
    case ast::Builtin::HashTableEntryGetHash:
    case ast::Builtin::HashTableEntryGetRow:
    case ast::Builtin::HashTableEntryGetNext: {
      CheckBuiltinHashTableEntryCall(call, builtin);
      break;
    }
    case ast::Builtin::SorterInit: {
      CheckBuiltinSorterInit(call);
      break;
    }
    case ast::Builtin::SorterInsert:
    case ast::Builtin::SorterInsertTopK:
    case ast::Builtin::SorterInsertTopKFinish: {
      CheckBuiltinSorterInsert(call, builtin);
      break;
    }
    case ast::Builtin::SorterSort:
    case ast::Builtin::SorterSortParallel:
    case ast::Builtin::SorterSortTopKParallel: {
      CheckBuiltinSorterSort(call, builtin);
      break;
    }
    case ast::Builtin::SorterFree: {
      CheckBuiltinSorterFree(call);
      break;
    }
    case ast::Builtin::SorterIterInit:
    case ast::Builtin::SorterIterHasNext:
    case ast::Builtin::SorterIterNext:
    case ast::Builtin::SorterIterSkipRows:
    case ast::Builtin::SorterIterGetRow:
    case ast::Builtin::SorterIterClose: {
      CheckBuiltinSorterIterCall(call, builtin);
      break;
    }
    case ast::Builtin::ResultBufferAllocOutRow:
    case ast::Builtin::ResultBufferFinalize: {
      CheckResultBufferCall(call, builtin);
      break;
    }
    case ast::Builtin::CSVReaderInit:
    case ast::Builtin::CSVReaderAdvance:
    case ast::Builtin::CSVReaderGetField:
    case ast::Builtin::CSVReaderGetRecordNumber:
    case ast::Builtin::CSVReaderClose: {
      CheckCSVReaderCall(call, builtin);
      break;
    }
    case ast::Builtin::ACos:
    case ast::Builtin::ASin:
    case ast::Builtin::ATan:
    case ast::Builtin::ATan2:
    case ast::Builtin::Cos:
    case ast::Builtin::Cot:
    case ast::Builtin::Sin:
    case ast::Builtin::Tan: {
      CheckMathTrigCall(call, builtin);
      break;
    }
    case ast::Builtin::SizeOf: {
      CheckBuiltinSizeOfCall(call);
      break;
    }
    case ast::Builtin::OffsetOf: {
      CheckBuiltinOffsetOfCall(call);
      break;
    }
    case ast::Builtin::PtrCast: {
      UNREACHABLE("Pointer cast should be handled outside switch ...");
    }
  }
}

}  // namespace tpl::sema
