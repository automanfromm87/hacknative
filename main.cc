#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char *argv[]) {
  std::string inputFile;
  std::string outputFile;
  std::string linkFlags;
  bool dumpAst = false;
  bool dumpIr = false;

  // Parse args: hackc <file.hack> [-o <output>] [--dump-ast] [--dump-ir] [--link-flags "..."]
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      outputFile = argv[++i];
    } else if (arg == "--dump-ast") {
      dumpAst = true;
    } else if (arg == "--dump-ir") {
      dumpIr = true;
    } else if (arg == "--link-flags" && i + 1 < argc) {
      linkFlags = argv[++i];
    } else if (inputFile.empty()) {
      inputFile = arg;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return 1;
    }
  }

  if (inputFile.empty()) {
    std::cerr << "Usage: hackc <file.hack> [-o <output>] [--dump-ast]\n";
    return 1;
  }

  // Read source file
  std::ifstream file(inputFile);
  if (!file) {
    std::cerr << "Cannot open file: " << inputFile << "\n";
    return 1;
  }
  std::stringstream buf;
  buf << file.rdbuf();
  std::string source = buf.str();

  // Resolve require directives: require "file.hhi";
  // Relative to input file's directory
  std::string inputDir;
  {
    auto slash = inputFile.rfind('/');
    inputDir = (slash != std::string::npos) ? inputFile.substr(0, slash + 1) : "";
  }
  {
    std::string expanded;
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
      // Match: require "path";
      size_t rpos = line.find("require");
      if (rpos != std::string::npos) {
        size_t q1 = line.find('"', rpos);
        size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
        if (q1 != std::string::npos && q2 != std::string::npos) {
          std::string reqPath = line.substr(q1 + 1, q2 - q1 - 1);
          std::string fullPath = inputDir + reqPath;
          std::ifstream reqFile(fullPath);
          if (!reqFile) {
            std::cerr << "Cannot open required file: " << fullPath << "\n";
            return 1;
          }
          std::stringstream reqBuf;
          reqBuf << reqFile.rdbuf();
          expanded += reqBuf.str() + "\n";
          continue;
        }
      }
      expanded += line + "\n";
    }
    source = expanded;
  }

  // Lex
  Lexer lexer(source);
  auto tokens = lexer.tokenize();

  // Parse
  Parser parser(tokens);
  Program prog;
  try {
    prog = parser.parse();
  } catch (const std::runtime_error &e) {
    std::cerr << "Parse error: " << e.what() << "\n";
    return 1;
  }

  if (dumpAst) {
    dumpAST(prog, std::cout);
    return 0;
  }

  // Codegen
  Codegen codegen;
  codegen.compile(prog);

  if (dumpIr) {
    codegen.dumpIR();
    return 0;
  }

  if (outputFile.empty()) {
    std::cerr << "Usage: hackc <file.hack> [-o <output>] [--dump-ast] [--dump-ir]\n";
    return 1;
  } else {
    // -o given: emit object file, then link with clang
    std::string objFile = outputFile + ".o";
    if (!codegen.emitObjectFile(objFile)) {
      return 1;
    }

    // Find runtime.c relative to the executable or in known locations
    // For now, compile runtime.c and link together
    std::string runtimeSrc = std::string(argv[0]);
    auto lastSlash = runtimeSrc.rfind('/');
    if (lastSlash != std::string::npos)
      runtimeSrc = runtimeSrc.substr(0, lastSlash + 1) + "../runtime.c";
    else
      runtimeSrc = "runtime.c";

    // If linking with SDL2, enable SDL2 helpers in runtime.c
    std::string runtimeFlags;
    if (linkFlags.find("SDL2") != std::string::npos) {
      runtimeFlags = " -DHACK_SDL2 $(sdl2-config --cflags)";
    }

    std::string linkCmd =
        "clang " + objFile + " " + runtimeSrc + runtimeFlags + " -o " + outputFile;
    if (!linkFlags.empty())
      linkCmd += " " + linkFlags;
    linkCmd += " 2>&1";
    int ret = std::system(linkCmd.c_str());
    std::remove(objFile.c_str());

    if (ret != 0) {
      std::cerr << "Linking failed\n";
      return 1;
    }
  }

  return 0;
}
