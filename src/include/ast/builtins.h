#pragma once

#include "util/common.h"

namespace tpl::ast {

#define BUILTINS_LIST(V)  \
  V(Filter, tpl_filter)   \
  V(Map, tpl_map)         \
  V(Fold, tpl_fold)       \
  V(Gather, tpl_gather)   \
  V(Scatter, tpl_scatter) \
  V(Compress, tpl_compress)

enum class Builtin : u8 {
#define ENTRY(Name, ...) Name,
  BUILTINS_LIST(ENTRY)
#undef ENTRY
#define COUNT_OP(inst, ...) +1
      Last = -1 BUILTINS_LIST(COUNT_OP)
#undef COUNT_OP
};

class Builtins {
 public:
  // The total number of builtin functions
  static const u32 kBuiltinsCount = static_cast<u32>(Builtin ::Last) + 1;

  // Return the total number of bytecodes
  static constexpr u32 NumBuiltins() { return kBuiltinsCount; }

  static const char *GetFunctionName(Builtin builtin) {
    return kBuiltinFunctionNames[static_cast<u8>(builtin)];
  }

 private:
  static const char *kBuiltinFunctionNames[];
};

}  // namespace tpl::ast