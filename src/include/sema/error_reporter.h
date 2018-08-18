#pragma once

#include <vector>

#include "ast/ast_value.h"
#include "common.h"
#include "parsing/token.h"
#include "sema/error_message.h"

namespace tpl::sema {

/**
 * TODO(pmenon): These don't use region vectors ...
 */
class ErrorReporter {
 public:
  // Record an error
  template <typename... ArgTypes>
  void Report(ErrorMessage<ArgTypes...> message, ArgTypes... args) {
    std::vector<SingleArg> typed_args = {SingleArg(std::move(args))...};
    errors_.emplace_back(message.id, std::move(typed_args));
  }

  // Have any errors been reported?
  bool has_errors() const { return !errors_.empty(); }

 private:
  /*
   * A single argument in the error message
   */
  class SingleArg {
   public:
    enum Kind {
      CString,
      Int,
      Token,
    };

    explicit SingleArg(const char *str) : kind_(Kind::CString), raw_str_(str) {}

    explicit SingleArg(int32_t integer) : kind_(Kind::Int), integer_(integer) {}

    explicit SingleArg(const ast::AstString *str) : SingleArg(str->bytes()) {}

    explicit SingleArg(parsing::Token::Type type)
        : SingleArg(
              static_cast<std::underlying_type_t<parsing::Token::Type>>(type)) {
      kind_ = Kind::Token;
    }

    Kind kind() const { return kind_; }

   private:
    Kind kind_;
    union {
      const char *raw_str_;
      int32_t integer_;
    };
  };

  /*
   * An encapsulated error message with proper argument types that can be
   * formatted and printed.
   */
  class MessageWithArgs {
   public:
    MessageWithArgs(ErrorMessageId id, std::vector<SingleArg> &&args)
        : id_(id), args_(std::move(args)) {}

    ErrorMessageId error_message_id() const { return id_; }

   private:
    ErrorMessageId id_;
    std::vector<SingleArg> args_;
  };

 private:
  std::vector<MessageWithArgs> errors_;
};

}  // namespace tpl::sema