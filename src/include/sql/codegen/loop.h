#pragma once

#include "common/common.h"
#include "sql/codegen/ast_fwd.h"

namespace tpl::sql::codegen {

class CodeGen;

/**
 * Helper class to generate TPL loops. Immediately after construction, statements appended to the
 * current active function are appended to the loop's body.
 *
 * @code
 * auto cond = codegen->CompareLt(a, b);
 * Loop loop(codegen, cond);
 * {
 *   // This code will appear in the "then" block of the statement.
 * }
 * loop.EndLoop();
 * @endcode
 */
class Loop {
 public:
  /**
   * Create a full loop.
   * @param codegen The code generator.
   * @param init The initialization statements.
   * @param condition The loop condition.
   * @param next The next statements.
   */
  Loop(CodeGen *codegen, ast::Stmt *init, ast::Expr *condition, ast::Stmt *next);

  /**
   * Create a while-loop.
   * @param codegen The code generator instance.
   * @param condition The loop condition.
   */
  Loop(CodeGen *codegen, ast::Expr *condition);

  /**
   * Create an infinite loop.
   * @param codegen The code generator instance.
   */
  explicit Loop(CodeGen *codegen);

  /**
   * Destructor.
   */
  ~Loop();

  /**
   * Explicitly mark the end of a loop.
   */
  void EndLoop();

 private:
  // The code generator instance.
  CodeGen *codegen_;
  // The loop position.
  const SourcePosition position_;
  // The previous list of statements.
  ast::BlockStmt *prev_statements_;
  // The initial statements, loop condition, and next statements.
  ast::Stmt *init_;
  ast::Expr *condition_;
  ast::Stmt *next_;
  // The loop body.
  ast::BlockStmt *loop_body_;
  // Completion flag.
  bool completed_;
};

}  // namespace tpl::sql::codegen
