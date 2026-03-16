#pragma once
#include "parser.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <map>
#include <memory>
#include <string>

class Codegen {
public:
  Codegen();
  void compile(const Program &prog);
  void dumpIR() const;
  bool emitObjectFile(const std::string &path) const;

private:
  void emitFunction(const FuncDecl &decl);
  void emitStmt(const Stmt &stmt, const FuncDecl &decl);
  void emitBlock(const std::vector<std::unique_ptr<Stmt>> &stmts, const FuncDecl &decl);
  llvm::Value *emitExpr(const Expr &expr);
  llvm::Type *getLLVMType(HackType t);
  HackType inferType(const Expr &expr);
  void emitClassDecl(const ClassDecl &cls);
  void declareRuntimeFunctions();
  void declareBuiltinFunctions();
  llvm::Value *emitInterfaceDispatch(const std::string &varName,
                                     llvm::Value *obj,
                                     const std::string &methodName,
                                     const std::vector<llvm::Value *> &extraArgs,
                                     DispatchStrategy strategy);

  llvm::LLVMContext context_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::map<std::string, std::pair<llvm::AllocaInst *, HackType>> namedValues_;
  std::map<std::string, HackType> funcTypes_;
  std::map<std::string, std::vector<HackType>> funcParamTypes_;

  // Class support
  struct ClassInfo {
    llvm::StructType *structType;
    std::vector<FieldDecl> fields;
    std::vector<FuncDecl*> methods;
    int fieldOffset = 0;           // 0 for normal, 1 for vtable-prepended
    bool implementsInterface = false;
    std::string interfaceName;
    llvm::GlobalVariable *vtableGlobal = nullptr;
  };
  std::map<std::string, ClassInfo> classTypes_;

  // Current class context (for $this)
  std::string currentClassName_;

  // Interface support
  struct InterfaceInfo {
    struct MethodSig {
      std::string name;
      HackType returnType;
      std::vector<Param> params;
    };
    std::vector<MethodSig> methods;
  };
  std::map<std::string, InterfaceInfo> interfaces_;

  // Dispatch tracking
  DispatchStrategy currentImplStrategy_ = DispatchStrategy::Vtable;
  std::map<std::string, DispatchStrategy> funcImplStrategy_;
  std::map<std::string, std::vector<std::string>> funcParamTypeNames_;

  // Variable type tracking
  std::map<std::string, std::string> varInterfaceName_; // var -> interface name
  std::map<std::string, std::string> varClassName_;     // var -> concrete class name
  std::map<std::string, llvm::AllocaInst *> fatPtrVtables_; // var -> vtable alloca (fatpointer)

  // Cached printf format string constants (avoid duplicates)
  std::map<std::string, llvm::Constant *> fmtStrings_;
  llvm::Constant *getOrCreateFmtString(const std::string &str, const std::string &name);

  // Plain GEP without inbounds/nuw
  llvm::Value *plainStructGEP(llvm::Type *structTy, llvm::Value *ptr,
                              unsigned idx, const llvm::Twine &name = "");
};
