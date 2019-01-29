#include <unistd.h>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

#include "llvm/Support/MemoryBuffer.h"

#include "ast/ast_dump.h"
#include "logging/logger.h"
#include "parsing/parser.h"
#include "parsing/scanner.h"
#include "sema/error_reporter.h"
#include "sema/sema.h"
#include "sql/catalog.h"
#include "tpl.h"
#include "util/timer.h"
#include "vm/bytecode_generator.h"
#include "vm/bytecode_module.h"
#include "vm/llvm_engine.h"
#include "vm/vm.h"

namespace tpl {

static constexpr const char *kExitKeyword = ".exit";

/// Compile the TPL source in \p source and run it in both interpreted and JIT
/// compiled mode
/// \param source The TPL source
/// \param name The name of the module/program
static void CompileAndRun(const std::string &source,
                          const std::string &name = "tmp-tpl") {
  util::Region region("repl-ast");
  util::Region error_region("repl-error");

  // Let's parse the source
  sema::ErrorReporter error_reporter(&error_region);
  ast::AstContext context(&region, error_reporter);

  parsing::Scanner scanner(source.data(), source.length());
  parsing::Parser parser(scanner, context);

  double parse_ms = 0, typecheck_ms = 0, codegen_ms = 0, exec_ms = 0,
         jit_ms = 0;

  // Parse
  ast::AstNode *root;
  {
    util::ScopedTimer<std::milli> timer(&parse_ms);
    root = parser.Parse();
  }

  if (error_reporter.HasErrors()) {
    LOG_ERROR("Parsing error!");
    error_reporter.PrintErrors();
    return;
  }

  // Type check
  {
    util::ScopedTimer<std::milli> timer(&typecheck_ms);
    sema::Sema type_check(context);
    type_check.Run(root);
  }

  if (error_reporter.HasErrors()) {
    LOG_ERROR("Type-checking error!");
    error_reporter.PrintErrors();
    return;
  }

  // Dump AST
  ast::AstDump::Dump(root);

  // Codegen
  std::unique_ptr<vm::BytecodeModule> module;
  {
    util::ScopedTimer<std::milli> timer(&codegen_ms);
    module = vm::BytecodeGenerator::Compile(&region, root, name);
  }

  // Dump VM
  module->PrettyPrint(std::cout);

  // Execute
  {
    util::ScopedTimer<std::milli> timer(&exec_ms);

    std::function<u32()> main_func;
    if (!module->GetFunction("main", vm::ExecutionMode::Interpret, main_func)) {
      LOG_ERROR("No main() entry function found with signature ()->int32");
      return;
    }

    LOG_INFO("VM main() returned: {}", main_func());
  }

  // JIT
  {
    util::ScopedTimer<std::milli> timer(&jit_ms);

    std::function<u32()> main_func;
    if (!module->GetFunction("main", vm::ExecutionMode::Jit, main_func)) {
      LOG_ERROR("No main() entry function found with signature ()->int32");
      return;
    }

    LOG_INFO("JIT main() returned: {}", main_func());
  }

  // Dump stats
  LOG_INFO(
      "Parse: {} ms, Type-check: {} ms, Code-gen: {} ms, Exec.: {} ms, "
      "Jit+Exec.: {} ms",
      parse_ms, typecheck_ms, codegen_ms, exec_ms, jit_ms);
}

/// Run the TPL REPL
static void RunRepl() {
  while (true) {
    std::string input;

    std::string line;
    do {
      printf(">>> ");
      std::getline(std::cin, line);

      if (line == kExitKeyword) {
        return;
      }

      input.append(line).append("\n");
    } while (!line.empty());

    CompileAndRun(input);
  }
}

/// Compile and run the TPL program in the given filename
/// \param filename The name of the file on disk to compile
static void RunFile(const std::string &filename) {
  auto file = llvm::MemoryBuffer::getFile(filename);
  if (std::error_code error = file.getError()) {
    LOG_ERROR("There was an error reading file '{}': {}", filename,
              error.message());
    return;
  }

  // Make a copy of the source ... crappy, yes, but okay in this use case
  const std::string source((*file)->getBufferStart(), (*file)->getBufferEnd());

  // Compile and run
  CompileAndRun(source);
}

/// Initialize all TPL subsystems
void InitTPL() {
  // Init logging
  tpl::logging::InitLogger();

  // Init catalog
  tpl::sql::Catalog::Instance();

  // Init LLVM engine
  tpl::vm::LLVMEngine::Initialize();
}

/// Shutdown all TPL subsystems
void ShutdownTPL() {
  // Shutdown LLVM
  tpl::vm::LLVMEngine::Shutdown();
}

}  // namespace tpl

void SignalHandler(i32 sig_num) {
  if (sig_num == SIGINT) {
    tpl::ShutdownTPL();
    exit(0);
  }
}

int main(int argc, char **argv) {
  // Initialize a signal handler
  struct sigaction sa;
  sa.sa_handler = &SignalHandler;
  sa.sa_flags = SA_RESTART;
  // Block every signal during the handler
  sigfillset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, nullptr) == -1) {
    perror("Error: cannot handle SIGINT");
  }

  // Init TPL
  tpl::InitTPL();

  LOG_INFO("Welcome to TPL (ver. {}.{})", TPL_VERSION_MAJOR, TPL_VERSION_MINOR);

  // Either execute a TPL program from a source file, or run REPL
  if (argc == 2) {
    std::string filename(argv[1]);
    tpl::RunFile(filename);
  } else if (argc == 1) {
    tpl::RunRepl();
  }

  // Cleanup
  tpl::ShutdownTPL();

  return 0;
}
