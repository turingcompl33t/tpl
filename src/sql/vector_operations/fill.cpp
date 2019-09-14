#include "sql/vector_operations/vector_operators.h"

#include "common/exception.h"

namespace tpl::sql {

namespace {

void CheckFillArguments(const Vector &input, const GenericValue &value) {
  if (input.type_id() != value.type_id()) {
    throw TypeMismatchException(input.type_id(), value.type_id(), "invalid types for fill");
  }
}

template <typename T>
void TemplatedFillOperation(Vector *vector, T val) {
  auto *data = reinterpret_cast<T *>(vector->data());
  VectorOps::Exec(*vector, [&](uint64_t i, uint64_t k) { data[i] = val; });
}

}  // namespace

void VectorOps::Fill(Vector *vector, const GenericValue &value) {
  // Sanity check
  CheckFillArguments(*vector, value);

  if (value.is_null()) {
    vector->mutable_null_mask()->SetAll();
    return;
  }

  vector->mutable_null_mask()->Reset();

  // Lift-off
  switch (vector->type_id()) {
    case TypeId::Boolean:
      TemplatedFillOperation(vector, value.value_.boolean);
      break;
    case TypeId::TinyInt:
      TemplatedFillOperation(vector, value.value_.tinyint);
      break;
    case TypeId::SmallInt:
      TemplatedFillOperation(vector, value.value_.smallint);
      break;
    case TypeId::Integer:
      TemplatedFillOperation(vector, value.value_.integer);
      break;
    case TypeId::BigInt:
      TemplatedFillOperation(vector, value.value_.bigint);
      break;
    case TypeId::Float:
      TemplatedFillOperation(vector, value.value_.float_);
      break;
    case TypeId::Double:
      TemplatedFillOperation(vector, value.value_.double_);
      break;
    case TypeId::Varchar:
      TemplatedFillOperation(vector, vector->strings_.AddString(value.str_value_));
      break;
    default:
      throw InvalidTypeException(vector->type_id(), "vector cannot be filled");
  }
}

void VectorOps::FillNull(Vector *vector) { vector->null_mask_.SetAll(); }

}  // namespace tpl::sql
