#pragma once

#include <type_traits>

#include "common/common.h"
#include "common/macros.h"
#include "sql/codegen/ast_fwd.h"
#include "sql/codegen/expression/column_value_provider.h"

namespace tpl::sql::planner {
class AbstractExpression;
}  // namespace tpl::sql::planner

namespace tpl::sql::codegen {

class CodeGen;
class CompilationContext;
class ConsumerContext;
class Pipeline;

/**
 * Base class for expression translators.
 */
class ExpressionTranslator {
 public:
  /**
   * Create a translator for an expression.
   * @param expr The expression.
   * @param compilation_context The context the translation occurs in.
   */
  ExpressionTranslator(const planner::AbstractExpression &expr,
                       CompilationContext *compilation_context);

  /**
   * This class cannot be copied or moved.
   */
  DISALLOW_COPY_AND_MOVE(ExpressionTranslator);

  /**
   * Destructor.
   */
  virtual ~ExpressionTranslator() = default;

  /**
   * Derive the TPL value of the expression.
   * @param ctx The consumer context.
   * @return The TPL value of the expression.
   */
  virtual ast::Expr *DeriveValue(ConsumerContext *ctx,
                                 const ColumnValueProvider *provider) const = 0;

  /**
   * @return The expression being translated.
   */
  const planner::AbstractExpression &GetExpression() const { return expr_; }

 protected:
  // The expression for this translator as its concrete type.
  template <typename T>
  const T &GetExpressionAs() const {
    static_assert(std::is_base_of_v<planner::AbstractExpression, T>,
                  "Template type is not an expression");
    return static_cast<const T &>(expr_);
  }

  // Return the code generation instance.
  CodeGen *GetCodeGen() const;

 private:
  // The expression that's to be translated.
  const planner::AbstractExpression &expr_;
  // The context the translation is a part of.
  CompilationContext *compilation_context_;
};

}  // namespace tpl::sql::codegen
