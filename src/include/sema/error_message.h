#pragma once

#include <cstdint>

namespace tpl::sema {

/*
 * The following macro lists out all the semantic and syntactic error messages
 * in TPL. Each macro has three parts: a globally unique textual message ID, the
 * templated string message that will be displayed, and the types of each
 * template argument.
 */
// clang-format off
#define MESSAGE_LIST(F)                                                                                      \
  F(UnexpectedToken, "unexpected token '$0', expecting '$1'", (const char *, const char *))                  \
  F(DuplicateArgName, "duplicate named argument '$0' in function '$0'", (const char *))                      \
  F(DuplicateStructFieldName, "duplicate field name '$0' in struct '$1'", (const char *, const char *))      \
  F(AssignmentUsedAsValue, "assignment '$0' = '$1' used as value", (const char *, const char *))             \
  F(ExpectingExpression, "expecting expression", ())                                                         \
  F(ExpectingType, "expecting type", ())
// clang-format on

/**
 * Define the ErrorMessageId enumeration
 */
#define F(id, str, arg_types) id,
enum class ErrorMessageId : uint32_t { MESSAGE_LIST(F) };
#undef F

/**
 * A templated struct that captures the ID of an error message and the argument
 * types that must be supplied when the error is reported. The template
 * arguments allow us to type-check to make sure users are providing all info.
 */
template <typename... ArgTypes>
struct ErrorMessage {
  const ErrorMessageId id;
};

/**
 * A container for all TPL error messages
 */
class ErrorMessages {
 public:
  template <typename T>
  struct ReflectErrorMessageWithDetails;

  template <typename... ArgTypes>
  struct ReflectErrorMessageWithDetails<void(ArgTypes...)> {
    using type = ErrorMessage<ArgTypes...>;
  };

#define MSG(kind, str, arg_types)                                              \
  static constexpr const ReflectErrorMessageWithDetails<void(arg_types)>::type \
      k##kind = {ErrorMessageId::kind};
  MESSAGE_LIST(MSG)
#undef MSG
};

}  // namespace tpl::sema