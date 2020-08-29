#pragma once

#include <memory>
#include <string>

#include "llvm/ADT/SmallVector.h"

#include "common/common.h"
#include "common/macros.h"
#include "sql/codegen/ast_fwd.h"

namespace tpl::vm {
class Module;
}  // namespace tpl::vm

namespace tpl::sql::codegen {

/**
 * A container for code in a single TPL file.
 */
class CompilationUnit {
  friend class CodeGen;

 public:
  /**
   * Create a new TPL code container.
   * @param ctx The AST context to use.
   * @param name The name of the container.
   */
  explicit CompilationUnit(ast::Context *ctx, std::string name);

  /**
   * This class cannot be copied or moved.
   */
  DISALLOW_COPY(CompilationUnit);

  /**
   * Register the given struct in this container.
   * @param decl The struct declaration.
   */
  void RegisterStruct(ast::StructDecl *decl) { structs_.push_back(decl); }

  /**
   * Declare the given function in this fragment. The provided function will not be marked for
   * direct invocation, but is available to other step functions for use.
   * @param decl The function declaration.
   */
  void RegisterFunction(ast::FunctionDecl *decl) { functions_.push_back(decl); }

  /**
   * Copy all declarations from the provided container into this one.
   * @pre Both contains must use the same AST context.
   * @param other The contains to copy declarations from.
   */
  void CopyDeclarations(const CompilationUnit &other);

  /**
   * Compile the code in the container.
   * @return The compiled module. If compilation fails, a null pointer is returned and any errors
   *         are dumped into the error reporter configured within the AST context.
   */
  std::unique_ptr<vm::Module> Compile();

  /**
   * @return The context.
   */
  ast::Context *Context() { return ctx_; }

 private:
  // The AST context used to generate the TPL AST.
  ast::Context *ctx_;
  // The name for the compilation unit generated by this container.
  std::string name_;
  // The list of all functions and structs.
  llvm::SmallVector<ast::StructDecl *, 16> structs_;
  llvm::SmallVector<ast::FunctionDecl *, 16> functions_;
};

}  // namespace tpl::sql::codegen
