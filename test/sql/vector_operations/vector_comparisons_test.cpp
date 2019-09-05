#include <vector>

#include "sql_test.h"

#include "sql/constant_vector.h"
#include "sql/vector.h"
#include "sql/vector_operations/vector_operators.h"

namespace tpl::sql {

class VectorComparisonTest : public TplTest {};

TEST_F(VectorComparisonTest, CompareNumeric) {
  //
  // This test compares two numeric vectors.
  //
  // The first input vector consists of six non-null big-ints: [0,1,2,3,4,5].
  //

  for (auto type_id : {TypeId::TinyInt, TypeId::SmallInt, TypeId::Integer,
                       TypeId::BigInt, TypeId::Float, TypeId::Double}) {
    auto vec = MakeBigIntVector({0, 1, 2, 3, 4, 5},
                                {false, false, false, false, false, false});
    vec->Cast(type_id);

    // Try to find
    ConstantVector _4(GenericValue::CreateBigInt(4).CastTo(type_id));
    auto result = MakeBooleanVector();

    // Check vec == 4. Only index 4 is valid.
    {
      const auto check = [&]() {
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(0));
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(1));
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(2));
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(3));
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(4));
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(5));
      };
      VectorOps::Equal(*vec, _4, result.get());
      check();
      VectorOps::Equal(_4, *vec, result.get());
      check();
    }

    // Check vec > 4. Only index 5 is valid.
    {
      VectorOps::GreaterThan(*vec, _4, result.get());
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(0));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(1));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(2));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(3));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(4));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(5));
    }

    // Check vec >= 4. Only indexes [4, 5] are valid.
    {
      VectorOps::GreaterThanEqual(*vec, _4, result.get());
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(0));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(1));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(2));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(3));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(4));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(5));
    }

    // Check vec < 4. Only indexes [0, 3] are valid.
    {
      VectorOps::LessThan(*vec, _4, result.get());
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(0));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(1));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(2));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(3));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(4));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(5));
    }

    // Check vec <= 4. Indexes [0, 4] are valid.
    {
      VectorOps::LessThanEqual(*vec, _4, result.get());
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(0));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(1));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(2));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(3));
      EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(4));
      EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(5));
    }

    // Check vec != 4. [0, 3] and [5, 5] are valid.
    {
      auto check = [&]() {
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(0));
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(1));
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(2));
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(3));
        EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(4));
        EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(5));
      };
      VectorOps::NotEqual(*vec, _4, result.get());
      check();
      VectorOps::NotEqual(_4, *vec, result.get());
      check();
    }
  }
}

TEST_F(VectorComparisonTest, CompareStrings) {
  //
  // String comparisons. We have two input vectors:
  //
  // a = ['first', 'second', NULL, 'fourth']
  // b = [NULL, 'second', NULL, 'baka not nice']
  //
  // We store the result of the comparison into the 'result' vector.
  //

  auto a = MakeVarcharVector({"first", "second", nullptr, "fourth"},
                             {false, false, true, false});
  auto b = MakeVarcharVector({nullptr, "second", nullptr, "baka not nice"},
                             {true, false, true, false});
  auto result = MakeBooleanVector();

  // a == b, only (1)
  VectorOps::Equal(*a, *b, result.get());
  EXPECT_EQ(4u, result->count());
  EXPECT_EQ(nullptr, result->selection_vector());
  EXPECT_TRUE(result->IsNull(0));
  EXPECT_EQ(GenericValue::CreateBoolean(true), result->GetValue(1));
  EXPECT_TRUE(result->IsNull(2));
  EXPECT_EQ(GenericValue::CreateBoolean(false), result->GetValue(3));
}

TEST_F(VectorComparisonTest, CompareWithNulls) {
  auto input = MakeBigIntVector({0, 1, 2, 3}, {false, false, false, false});
  auto null = ConstantVector(GenericValue::CreateNull(TypeId::BigInt));
  auto result = MakeBooleanVector();

  VectorOps::Equal(*input, null, result.get());
  EXPECT_TRUE(result->IsNull(0));
  EXPECT_TRUE(result->IsNull(1));
  EXPECT_TRUE(result->IsNull(2));
  EXPECT_TRUE(result->IsNull(3));
}

}  // namespace tpl::sql