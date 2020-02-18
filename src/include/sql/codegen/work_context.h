#pragma once

#include <functional>
#include <unordered_map>
#include <utility>

#include "common/common.h"
#include "sql/codegen/ast_fwd.h"
#include "sql/codegen/expression/expression_translator.h"
#include "sql/codegen/pipeline.h"

namespace tpl::sql::planner {
class AbstractExpression;
}  // namespace tpl::sql::planner

namespace tpl::sql::codegen {

class CompilationContext;
class FunctionBuilder;
class Pipeline;

/**
 * A work context carries information necessary for a pipeline along all operators within that
 * pipeline. It provides access to thread-local state and a mechanism to evaluation expressions in
 * the pipeline.
 */
class WorkContext {
 public:
  /**
   * Create a new context whose data flows along the provided pipeline.
   * @param compilation_context The compilation context.
   * @param pipeline The pipeline.
   */
  WorkContext(CompilationContext *compilation_context, const Pipeline &pipeline);

  /**
   * Derive the value of the given expression.
   * @param expr The expression.
   * @return The TPL value of the expression.
   */
  ast::Expr *DeriveValue(const planner::AbstractExpression &expr,
                         const ColumnValueProvider *provider);

  /**
   * Push this context through to the next step in the pipeline.
   * @param function The function that's being built.
   */
  void Push(FunctionBuilder *function);

  /**
   * Clear any cached expression result values.
   */
  void ClearExpressionCache();

  /**
   * @return The operator the context is currently positioned at in the pipeline.
   */
  OperatorTranslator *CurrentOp() const { return *pipeline_iter_; }

  /**
   * @return The pipeline the consumption occurs in.
   */
  const Pipeline &GetPipeline() const { return pipeline_; }

  /**
   * Controls whether expression caching is enabled in this context.
   * @param val True if caching is enabled; false otherwise.
   */
  void SetExpressionCacheEnable(bool val) { cache_enabled_ = val; }

 private:
  // The compilation context.
  CompilationContext *compilation_context_;
  // The pipeline that this context flows through.
  const Pipeline &pipeline_;
  // Cache of expression results.
  std::unordered_map<const planner::AbstractExpression *, ast::Expr *> cache_;
  // The current pipeline step and last pipeline step.
  Pipeline::StepIterator pipeline_iter_, pipeline_end_;
  // Whether to cache translated expressions
  bool cache_enabled_;
};

}  // namespace tpl::sql::codegen
