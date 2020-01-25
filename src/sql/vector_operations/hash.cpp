#include "sql/vector_operations/vector_operators.h"

#include "common/exception.h"
#include "sql/operations/hash_operators.h"

namespace tpl::sql {

namespace {

void CheckHashArguments(const Vector &input, Vector *result) {
  if (result->GetTypeId() != TypeId::Hash) {
    throw InvalidTypeException(result->GetTypeId(), "Output of Hash() operation must be hash");
  }
}

template <typename InputType>
void TemplatedHashOperation(const Vector &input, Vector *result) {
  auto *RESTRICT input_data = reinterpret_cast<InputType *>(input.GetData());
  auto *RESTRICT result_data = reinterpret_cast<hash_t *>(result->GetData());

  result->Resize(input.GetSize());
  result->GetMutableNullMask()->Reset();
  result->SetFilteredTupleIdList(input.GetFilteredTupleIdList(), input.GetCount());

  if (input.GetNullMask().Any()) {
    VectorOps::Exec(input, [&](uint64_t i, uint64_t k) {
      result_data[i] = tpl::sql::Hash<InputType>::Apply(input_data[i], input.GetNullMask()[i]);
    });
  } else {
    VectorOps::Exec(input, [&](uint64_t i, uint64_t k) {
      result_data[i] = tpl::sql::Hash<InputType>::Apply(input_data[i], false);
    });
  }
}

template <typename InputType>
void TemplatedHashCombineOperation(const Vector &input, Vector *result) {
  auto *RESTRICT input_data = reinterpret_cast<InputType *>(input.GetData());
  auto *RESTRICT result_data = reinterpret_cast<hash_t *>(result->GetData());

  result->Resize(input.GetSize());
  result->GetMutableNullMask()->Reset();
  result->SetFilteredTupleIdList(input.GetFilteredTupleIdList(), input.GetCount());

  if (input.GetNullMask().Any()) {
    VectorOps::Exec(input, [&](uint64_t i, uint64_t k) {
      result_data[i] = tpl::sql::HashCombine<InputType>::Apply(
          input_data[i], input.GetNullMask()[i], result_data[i]);
    });
  } else {
    VectorOps::Exec(input, [&](uint64_t i, uint64_t k) {
      result_data[i] =
          tpl::sql::HashCombine<InputType>::Apply(input_data[i], false, result_data[i]);
    });
  }
}

}  // namespace

void VectorOps::Hash(const Vector &input, Vector *result) {
  // Sanity check
  CheckHashArguments(input, result);

  // Lift-off
  switch (input.GetTypeId()) {
    case TypeId::Boolean:
      TemplatedHashOperation<bool>(input, result);
      break;
    case TypeId::TinyInt:
      TemplatedHashOperation<int8_t>(input, result);
      break;
    case TypeId::SmallInt:
      TemplatedHashOperation<int16_t>(input, result);
      break;
    case TypeId::Integer:
      TemplatedHashOperation<int32_t>(input, result);
      break;
    case TypeId::BigInt:
      TemplatedHashOperation<int64_t>(input, result);
      break;
    case TypeId::Float:
      TemplatedHashOperation<float>(input, result);
      break;
    case TypeId::Double:
      TemplatedHashOperation<double>(input, result);
      break;
    case TypeId::Date:
      TemplatedHashOperation<Date>(input, result);
      break;
    case TypeId::Varchar:
      TemplatedHashOperation<VarlenEntry>(input, result);
      break;
    default:
      throw NotImplementedException("hashing vector type '{}'", TypeIdToString(input.GetTypeId()));
  }
}

void VectorOps::HashCombine(const Vector &input, Vector *result) {
  // Sanity check
  CheckHashArguments(input, result);

  // Lift-off
  switch (input.GetTypeId()) {
    case TypeId::Boolean:
      TemplatedHashCombineOperation<bool>(input, result);
      break;
    case TypeId::TinyInt:
      TemplatedHashCombineOperation<int8_t>(input, result);
      break;
    case TypeId::SmallInt:
      TemplatedHashCombineOperation<int16_t>(input, result);
      break;
    case TypeId::Integer:
      TemplatedHashCombineOperation<int32_t>(input, result);
      break;
    case TypeId::BigInt:
      TemplatedHashCombineOperation<int64_t>(input, result);
      break;
    case TypeId::Float:
      TemplatedHashCombineOperation<float>(input, result);
      break;
    case TypeId::Double:
      TemplatedHashCombineOperation<double>(input, result);
      break;
    case TypeId::Date:
      TemplatedHashCombineOperation<Date>(input, result);
      break;
    case TypeId::Varchar:
      TemplatedHashCombineOperation<VarlenEntry>(input, result);
      break;
    default:
      throw NotImplementedException("hashing vector type '{}'", TypeIdToString(input.GetTypeId()));
  }
}

}  // namespace tpl::sql
