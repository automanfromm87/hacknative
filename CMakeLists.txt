cmake_minimum_required(VERSION 3.20)
project(hacknative LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM installed via Homebrew
find_package(LLVM REQUIRED CONFIG
  PATHS /opt/homebrew/opt/llvm/lib/cmake/llvm
  NO_DEFAULT_PATH)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(hackc
  main.cc
  lexer.cc
  parser.cc
  codegen.cc
)

llvm_map_components_to_libnames(llvm_libs
  Core Support IRReader
  Target AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info
  X86CodeGen X86AsmParser X86Desc X86Info
  MC MCParser
)
target_link_libraries(hackc ${llvm_libs})
