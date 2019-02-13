#pragma once

#include "ast/ast_visitor.h"
#include "util/common.h"
#include "util/macros.h"

namespace tpl::ast {

/**
 * A visitor that fully traverses an AST tree
 *
 * @tparam Subclass
 */
template <typename Subclass>
class AstTraversalVisitor : public AstVisitor<Subclass> {
 public:
  explicit AstTraversalVisitor(AstNode *root) : root_(root) {}

  DISALLOW_COPY_AND_MOVE(AstTraversalVisitor);

  void Run() {
    TPL_ASSERT(root_ != nullptr, "Cannot run traversal on NULL tree");
    AstVisitor<Subclass>::Visit(root_);
  }

  // Declare all node visit methods here
#define DECLARE_VISIT_METHOD(type) void Visit##type(ast::type *node);
  AST_NODES(DECLARE_VISIT_METHOD)
#undef DECLARE_VISIT_METHOD

 protected:
  bool VisitNode(AstNode *node) { return true; }

 private:
  AstNode *root_;
};

////////////////////////////////////////////////////////////////////////////////
///
/// Implementation below
///
////////////////////////////////////////////////////////////////////////////////

#define PROCESS_NODE(node)                \
  do {                                    \
    if (!this->impl()->VisitNode(node)) { \
      return;                             \
    }                                     \
  } while (false);

#define RECURSE(call) this->impl()->call;

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitBadExpr(BadExpr *node) {
  PROCESS_NODE(node);
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitFieldDecl(FieldDecl *node) {
  PROCESS_NODE(node);
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitFunctionDecl(
    FunctionDecl *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->function()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitIdentifierExpr(
    IdentifierExpr *node) {
  PROCESS_NODE(node);
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitArrayTypeRepr(
    ArrayTypeRepr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->element_type()));
  if (node->HasLength()) {
    RECURSE(Visit(node->length()));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitBlockStmt(BlockStmt *node) {
  PROCESS_NODE(node);
  for (auto *stmt : node->statements()) {
    RECURSE(Visit(stmt));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitStructDecl(StructDecl *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->type_repr()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitVariableDecl(
    VariableDecl *node) {
  PROCESS_NODE(node);
  if (node->HasTypeDecl()) {
    RECURSE(Visit(node->type_repr()));
  }
  if (node->HasInitialValue()) {
    RECURSE(Visit(node->initial()));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitUnaryOpExpr(UnaryOpExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->expr()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitReturnStmt(ReturnStmt *node) {
  PROCESS_NODE(node);
  if (node->HasExpressionValue()) {
    RECURSE(Visit(node->ret()));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitCallExpr(CallExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->function()));
  for (auto *arg : node->arguments()) {
    RECURSE(Visit(arg));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitImplicitCastExpr(
    ImplicitCastExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->input()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitAssignmentStmt(
    AssignmentStmt *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->destination()));
  RECURSE(Visit(node->source()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitFile(File *node) {
  PROCESS_NODE(node);
  for (auto *decl : node->declarations()) {
    RECURSE(Visit(decl));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitFunctionLitExpr(
    FunctionLitExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->type_repr()));
  RECURSE(Visit(node->body()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitForStmt(ForStmt *node) {
  PROCESS_NODE(node);
  if (node->HasInitializer()) {
    RECURSE(Visit(node->init()));
  }
  if (node->HasCondition()) {
    RECURSE(Visit(node->condition()));
  }
  if (node->HasNext()) {
    RECURSE(Visit(node->next()));
  }
  RECURSE(Visit(node->body()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitForInStmt(ForInStmt *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->target()));
  RECURSE(Visit(node->iter()));
  RECURSE(Visit(node->body()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitBinaryOpExpr(
    BinaryOpExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->left()));
  RECURSE(Visit(node->right()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitMapTypeRepr(MapTypeRepr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->key()));
  RECURSE(Visit(node->val()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitLitExpr(LitExpr *node) {
  PROCESS_NODE(node);
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitStructTypeRepr(
    StructTypeRepr *node) {
  PROCESS_NODE(node);
  for (auto *field : node->fields()) {
    RECURSE(Visit(field));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitDeclStmt(DeclStmt *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->declaration()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitMemberExpr(MemberExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->object()));
  RECURSE(Visit(node->member()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitPointerTypeRepr(
    PointerTypeRepr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->base()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitComparisonOpExpr(
    ComparisonOpExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->left()));
  RECURSE(Visit(node->right()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitIfStmt(IfStmt *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->condition()));
  RECURSE(Visit(node->then_stmt()));
  if (node->HasElseStmt()) {
    RECURSE(Visit(node->else_stmt()));
  }
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitExpressionStmt(
    ExpressionStmt *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->expression()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitIndexExpr(IndexExpr *node) {
  PROCESS_NODE(node);
  RECURSE(Visit(node->object()));
  RECURSE(Visit(node->index()));
}

template <typename Subclass>
inline void AstTraversalVisitor<Subclass>::VisitFunctionTypeRepr(
    FunctionTypeRepr *node) {
  PROCESS_NODE(node);
  for (auto *param : node->parameters()) {
    RECURSE(Visit(param));
  }
  RECURSE(Visit(node->return_type()));
}

}  // namespace tpl::ast
