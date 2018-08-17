#pragma once

#include "ast/ast.h"
#include "ast/ast_node_factory.h"
#include "ast/ast_value.h"
#include "parsing/scanner.h"
#include "sema/error_reporter.h"

namespace tpl::parsing {

class Parser {
 public:
  Parser(Scanner &scanner, ast::AstNodeFactory &node_factory,
         ast::AstStringsContainer &strings_container,
         sema::ErrorReporter &error_reporter);

  /**
   * Parse and generate an abstract syntax tree from the input TPL source code
   */
  ast::AstNode *Parse();

 private:
  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Simple accessors
  ///
  //////////////////////////////////////////////////////////////////////////////

  Scanner &scanner() { return scanner_; }

  ast::AstNodeFactory &node_factory() { return node_factory_; }

  util::Region &region() { return node_factory().region(); }

  ast::AstStringsContainer &strings_container() { return strings_container_; }

  sema::ErrorReporter &error_reporter() { return error_reporter_; }

  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Token logic
  ///
  //////////////////////////////////////////////////////////////////////////////

  Token::Type Next() { return scanner().Next(); }

  Token::Type peek() { return scanner().peek(); }

  void Consume(UNUSED Token::Type expected) {
    UNUSED Token::Type next = Next();
#ifndef NDEBUG
    if (next != expected) {
      error_reporter().Report(sema::ErrorMessages::kUnexpectedToken,
                              Token::String(next), Token::String(expected));
    }
#endif
  }

  void Expect(Token::Type expected) {
    Token::Type next = Next();
    if (next != expected) {
      error_reporter().Report(sema::ErrorMessages::kUnexpectedToken,
                              Token::String(next), Token::String(expected));
    }
  }

  bool Matches(Token::Type expected) {
    if (peek() != expected) {
      return false;
    }

    Consume(expected);

    return true;
  }

  // Get the current symbol as an AST string
  ast::AstString *GetSymbol() {
    const std::string &literal = scanner().current_literal();
    return strings_container().GetAstString(literal);
  }

  //////////////////////////////////////////////////////////////////////////////
  ///
  /// Parsing productions
  ///
  //////////////////////////////////////////////////////////////////////////////

  ast::Declaration *ParseDeclaration();

  ast::Declaration *ParseFunctionDeclaration();

  ast::Declaration *ParseStructDeclaration();

  ast::Declaration *ParseVariableDeclaration();

  ast::Statement *ParseStatement();

  ast::Statement *ParseSimpleStatement();

  ast::Statement *ParseBlockStatement();

  struct ForHeader {
    ast::Statement *init;
    ast::Expression *cond;
    ast::Statement *next;
  };

  ForHeader ParseForHeader();

  ast::Statement *ParseForStatement();

  ast::Statement *ParseIfStatement();

  ast::Statement *ParseReturnStatement();

  ast::Expression *ParseExpression();

  ast::Expression *ParseBinaryExpression(uint32_t min_prec);

  ast::Expression *ParseUnaryExpression();

  ast::Expression *ParseCallExpression();

  ast::Expression *ParsePrimaryExpression();

  ast::Expression *ParseFunctionLiteralExpression();

  ast::Expression *ParseType();

  ast::Expression *ParseFunctionType();

  ast::Expression *ParsePointerType();

  ast::Expression *ParseArrayType();

  ast::Expression *ParseStructType();

 private:
  // The source code scanner
  Scanner &scanner_;

  // A factory for all node types
  ast::AstNodeFactory &node_factory_;

  // A factory for strings
  ast::AstStringsContainer &strings_container_;

  // The error reporter
  sema::ErrorReporter &error_reporter_;
};

}  // namespace tpl::parsing