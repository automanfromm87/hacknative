#include "codegen.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

Codegen::Codegen() : module_(std::make_unique<llvm::Module>("hacknative", context_)),
                     builder_(context_) {}

// Plain GEP without inbounds/nuw — direct, no optimization hints
llvm::Value *Codegen::plainStructGEP(llvm::Type *structTy, llvm::Value *ptr,
                                     unsigned idx, const llvm::Twine &name) {
  return builder_.CreateGEP(
      structTy, ptr,
      {builder_.getInt32(0), builder_.getInt32(idx)},
      name);
}

llvm::Constant *Codegen::getOrCreateFmtString(const std::string &str, const std::string &name) {
  auto it = fmtStrings_.find(str);
  if (it != fmtStrings_.end())
    return it->second;
  auto *gv = builder_.CreateGlobalString(str, name);
  fmtStrings_[str] = gv;
  return gv;
}

llvm::Type *Codegen::getLLVMType(HackType t) {
  switch (t) {
  case HackType::Int:    return llvm::Type::getInt32Ty(context_);
  case HackType::Float:  return llvm::Type::getDoubleTy(context_);
  case HackType::Bool:   return llvm::Type::getInt1Ty(context_);
  case HackType::String: return llvm::PointerType::getUnqual(context_);
  case HackType::Void:   return llvm::Type::getVoidTy(context_);
  case HackType::Vec:    return llvm::PointerType::getUnqual(context_);
  case HackType::Dict:   return llvm::PointerType::getUnqual(context_);
  case HackType::Object: return llvm::PointerType::getUnqual(context_);
  }
  return llvm::Type::getInt32Ty(context_);
}

HackType Codegen::inferType(const Expr &expr) {
  switch (expr.kind) {
  case ExprKind::IntLiteral:    return HackType::Int;
  case ExprKind::FloatLiteral:  return HackType::Float;
  case ExprKind::BoolLiteral:   return HackType::Bool;
  case ExprKind::StringLiteral: return HackType::String;
  case ExprKind::VecLiteral:    return HackType::Vec;
  case ExprKind::DictLiteral:   return HackType::Dict;
  case ExprKind::New:           return HackType::Object;
  case ExprKind::VarRef: {
    auto &ref = static_cast<const VarRefExpr &>(expr);
    auto it = namedValues_.find(ref.name);
    if (it != namedValues_.end())
      return it->second.second;
    return HackType::Int;
  }
  case ExprKind::UnaryOp: {
    auto &unary = static_cast<const UnaryExpr &>(expr);
    if (unary.op == UnaryOpKind::Not)
      return HackType::Bool;
    return inferType(*unary.operand);
  }
  case ExprKind::BinaryOp: {
    auto &bin = static_cast<const BinaryExpr &>(expr);
    if (bin.op == BinOp::Eq || bin.op == BinOp::Neq ||
        bin.op == BinOp::Lt || bin.op == BinOp::Gt ||
        bin.op == BinOp::Le || bin.op == BinOp::Ge ||
        bin.op == BinOp::And || bin.op == BinOp::Or)
      return HackType::Bool;
    if (bin.op == BinOp::Concat)
      return HackType::String;
    HackType lt = inferType(*bin.lhs);
    HackType rt = inferType(*bin.rhs);
    if (lt == HackType::Float || rt == HackType::Float)
      return HackType::Float;
    return HackType::Int;
  }
  case ExprKind::FuncCall: {
    auto &call = static_cast<const FuncCallExpr &>(expr);
    if (call.name == "strlen" || call.name == "intval" || call.name == "count")
      return HackType::Int;
    if (call.name == "substr" || call.name == "str_repeat")
      return HackType::String;
    auto it = funcTypes_.find(call.name);
    if (it != funcTypes_.end())
      return it->second;
    return HackType::Int;
  }
  case ExprKind::Subscript:
    return HackType::Int;
  case ExprKind::MemberAccess: {
    auto &ma = static_cast<const MemberAccessExpr &>(expr);
    HackType objType = inferType(*ma.object);
    if (objType == HackType::Object) {
      if (ma.object->kind == ExprKind::VarRef) {
        auto &ref = static_cast<const VarRefExpr &>(*ma.object);
        // Check varClassName_ to find the right class
        auto classIt = varClassName_.find(ref.name);
        if (classIt != varClassName_.end()) {
          auto infoIt = classTypes_.find(classIt->second);
          if (infoIt != classTypes_.end()) {
            for (auto &f : infoIt->second.fields) {
              if (f.name == ma.member)
                return f.type;
            }
          }
        }
        // Fallback: search all classes
        for (auto &[name, info] : classTypes_) {
          for (auto &f : info.fields) {
            if (f.name == ma.member)
              return f.type;
          }
        }
      }
    }
    return HackType::Int;
  }
  case ExprKind::MethodCall: {
    auto &mc = static_cast<const MethodCallExpr &>(expr);
    // Check interfaces first
    for (auto &[name, info] : interfaces_) {
      for (auto &m : info.methods) {
        if (m.name == mc.method)
          return m.returnType;
      }
    }
    for (auto &[name, info] : classTypes_) {
      for (auto *m : info.methods) {
        if (m->name == mc.method)
          return m->hackReturnType;
      }
    }
    return HackType::Int;
  }
  }
  return HackType::Int;
}

void Codegen::declareRuntimeFunctions() {
  auto *ptrTy = llvm::PointerType::getUnqual(context_);
  auto *i32Ty = llvm::Type::getInt32Ty(context_);
  auto *voidTy = llvm::Type::getVoidTy(context_);

  module_->getOrInsertFunction("hack_strcat",
      llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false));
  module_->getOrInsertFunction("hack_vec_new",
      llvm::FunctionType::get(ptrTy, {}, false));
  module_->getOrInsertFunction("hack_vec_push",
      llvm::FunctionType::get(voidTy, {ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_vec_get",
      llvm::FunctionType::get(i32Ty, {ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_vec_set",
      llvm::FunctionType::get(voidTy, {ptrTy, i32Ty, i32Ty}, false));
  module_->getOrInsertFunction("hack_vec_size",
      llvm::FunctionType::get(i32Ty, {ptrTy}, false));
  module_->getOrInsertFunction("hack_dict_new",
      llvm::FunctionType::get(ptrTy, {}, false));
  module_->getOrInsertFunction("hack_dict_set",
      llvm::FunctionType::get(voidTy, {ptrTy, ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_dict_get",
      llvm::FunctionType::get(i32Ty, {ptrTy, ptrTy}, false));
  module_->getOrInsertFunction("hack_dict_size",
      llvm::FunctionType::get(i32Ty, {ptrTy}, false));
  module_->getOrInsertFunction("hack_dict_key_at",
      llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_dict_val_at",
      llvm::FunctionType::get(i32Ty, {ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_strlen",
      llvm::FunctionType::get(i32Ty, {ptrTy}, false));
  module_->getOrInsertFunction("hack_substr",
      llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty, i32Ty}, false));
  module_->getOrInsertFunction("hack_intval",
      llvm::FunctionType::get(i32Ty, {ptrTy}, false));
  module_->getOrInsertFunction("hack_str_repeat",
      llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty}, false));
  module_->getOrInsertFunction("hack_print_r_vec",
      llvm::FunctionType::get(voidTy, {ptrTy}, false));
  module_->getOrInsertFunction("hack_print_r_dict",
      llvm::FunctionType::get(voidTy, {ptrTy}, false));
  module_->getOrInsertFunction("printf",
      llvm::FunctionType::get(i32Ty, {ptrTy}, true));
  module_->getOrInsertFunction("malloc",
      llvm::FunctionType::get(ptrTy, {llvm::Type::getInt64Ty(context_)}, false));
}

void Codegen::emitClassDecl(const ClassDecl &cls) {
  auto *ptrTy = llvm::PointerType::getUnqual(context_);
  bool hasInterface = !cls.implementsInterface.empty();

  // Create struct type: if implements interface, prepend a vtable pointer
  std::vector<llvm::Type *> fieldTypes;
  int fieldOffset = 0;
  if (hasInterface) {
    fieldTypes.push_back(ptrTy); // vtable pointer at index 0
    fieldOffset = 1;
  }
  for (auto &f : cls.fields) {
    fieldTypes.push_back(getLLVMType(f.type));
  }
  auto *structTy = llvm::StructType::create(context_, fieldTypes, cls.name);

  ClassInfo info;
  info.structType = structTy;
  info.fields = cls.fields;
  info.fieldOffset = fieldOffset;
  info.implementsInterface = hasInterface;
  info.interfaceName = cls.implementsInterface;

  // Declare methods
  for (auto &method : cls.methods) {
    std::string funcName = cls.name + "." + method.name;
    std::vector<llvm::Type *> paramTypes;
    paramTypes.push_back(ptrTy); // $this
    std::vector<HackType> paramHackTypes;
    paramHackTypes.push_back(HackType::Object);
    for (auto &p : method.params) {
      paramTypes.push_back(getLLVMType(p.type));
      paramHackTypes.push_back(p.type);
    }
    llvm::Type *retTy = getLLVMType(method.hackReturnType);
    auto *funcType = llvm::FunctionType::get(retTy, paramTypes, false);
    llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                           funcName, module_.get());
    funcTypes_[funcName] = method.hackReturnType;
    funcParamTypes_[funcName] = std::move(paramHackTypes);
  }

  classTypes_[cls.name] = std::move(info);
}

llvm::Value *Codegen::emitInterfaceDispatch(const std::string &varName,
                                            llvm::Value *obj,
                                            const std::string &methodName,
                                            const std::vector<llvm::Value *> &extraArgs,
                                            DispatchStrategy strategy) {
  auto ifaceIt = varInterfaceName_.find(varName);
  if (ifaceIt == varInterfaceName_.end()) return builder_.getInt32(0);
  const std::string &ifaceName = ifaceIt->second;

  auto infoIt = interfaces_.find(ifaceName);
  if (infoIt == interfaces_.end()) return builder_.getInt32(0);

  // Find method index in interface
  int methodIdx = -1;
  HackType retType = HackType::Int;
  for (size_t i = 0; i < infoIt->second.methods.size(); ++i) {
    if (infoIt->second.methods[i].name == methodName) {
      methodIdx = (int)i;
      retType = infoIt->second.methods[i].returnType;
      break;
    }
  }
  if (methodIdx < 0) return builder_.getInt32(0);

  auto *ptrTy = llvm::PointerType::getUnqual(context_);
  llvm::Type *llvmRetTy = getLLVMType(retType);

  // Build function type: (ptr obj, extraArgs...)
  std::vector<llvm::Type *> callParamTypes;
  callParamTypes.push_back(ptrTy); // $this / data ptr
  for (auto *a : extraArgs)
    callParamTypes.push_back(a->getType());
  auto *fnTy = llvm::FunctionType::get(llvmRetTy, callParamTypes, false);

  // A struct type with just a pointer (for accessing vtable slot at index 0)
  std::vector<llvm::Type *> vtSlotTypes = {ptrTy};
  auto *vtSlotStructTy = llvm::StructType::get(context_, vtSlotTypes);

  if (strategy == DispatchStrategy::Vtable) {
    // load vtable from obj[0], GEP slot, indirect call
    auto *vtableSlot = plainStructGEP(
        vtSlotStructTy, obj, 0, "vtable.slot");
    auto *vtablePtr = builder_.CreateLoad(ptrTy, vtableSlot, "vtable");
    // GEP into vtable array
    auto *vtableArrayTy = llvm::ArrayType::get(ptrTy, infoIt->second.methods.size());
    auto *fnSlot = builder_.CreateGEP(vtableArrayTy, vtablePtr,
        {builder_.getInt32(0), builder_.getInt32(methodIdx)}, "fn.slot");
    auto *fn = builder_.CreateLoad(ptrTy, fnSlot, "fn");
    std::vector<llvm::Value *> args;
    args.push_back(obj);
    args.insert(args.end(), extraArgs.begin(), extraArgs.end());
    if (llvmRetTy->isVoidTy())
      return builder_.CreateCall(fnTy, fn, args);
    return builder_.CreateCall(fnTy, fn, args, "vcall");
  }

  if (strategy == DispatchStrategy::FatPointer) {
    // vtable is stored in fatPtrVtables_[varName]
    auto fpIt = fatPtrVtables_.find(varName);
    if (fpIt == fatPtrVtables_.end()) return builder_.getInt32(0);
    auto *vtablePtr = builder_.CreateLoad(ptrTy, fpIt->second, "fp.vtable");
    auto *vtableArrayTy = llvm::ArrayType::get(ptrTy, infoIt->second.methods.size());
    auto *fnSlot = builder_.CreateGEP(vtableArrayTy, vtablePtr,
        {builder_.getInt32(0), builder_.getInt32(methodIdx)}, "fp.fn.slot");
    auto *fn = builder_.CreateLoad(ptrTy, fnSlot, "fp.fn");
    std::vector<llvm::Value *> args;
    args.push_back(obj);
    args.insert(args.end(), extraArgs.begin(), extraArgs.end());
    if (llvmRetTy->isVoidTy())
      return builder_.CreateCall(fnTy, fn, args);
    return builder_.CreateCall(fnTy, fn, args, "fpcall");
  }

  if (strategy == DispatchStrategy::TypeTag) {
    // load vtable ptr from obj[0], compare with known vtables, branch to direct calls
    auto *vtableSlot = plainStructGEP(
        vtSlotStructTy, obj, 0, "tt.vtable.slot");
    auto *vtablePtr = builder_.CreateLoad(ptrTy, vtableSlot, "tt.vtable");

    auto *curFunc = builder_.GetInsertBlock()->getParent();

    // Collect all classes implementing this interface
    std::vector<std::pair<std::string, ClassInfo*>> implClasses;
    for (auto &[cn, ci] : classTypes_) {
      if (ci.implementsInterface && ci.interfaceName == ifaceName) {
        implClasses.push_back({cn, &ci});
      }
    }

    auto *mergeBB = llvm::BasicBlock::Create(context_, "tt.merge", curFunc);
    llvm::PHINode *phi = nullptr;
    if (!llvmRetTy->isVoidTy()) {
      // We'll set up phi after creating all branches
    }

    // Build chain of comparisons
    struct BranchResult {
      llvm::BasicBlock *bb;
      llvm::Value *val;
    };
    std::vector<BranchResult> results;

    // Create a default/fallback BB that also branches to merge
    auto *defaultBB = llvm::BasicBlock::Create(context_, "tt.default", curFunc);

    for (size_t i = 0; i < implClasses.size(); ++i) {
      auto &[cn, ci] = implClasses[i];
      auto *thenBB = llvm::BasicBlock::Create(context_, "tt." + cn, curFunc);
      auto *nextBB = (i + 1 < implClasses.size())
          ? llvm::BasicBlock::Create(context_, "tt.next", curFunc)
          : defaultBB;

      auto *cmp = builder_.CreateICmpEQ(vtablePtr, ci->vtableGlobal, "tt.cmp." + cn);
      builder_.CreateCondBr(cmp, thenBB, nextBB);

      builder_.SetInsertPoint(thenBB);
      std::string directName = cn + "." + methodName;
      auto *directFn = module_->getFunction(directName);
      llvm::Value *result = nullptr;
      if (directFn) {
        std::vector<llvm::Value *> args;
        args.push_back(obj);
        args.insert(args.end(), extraArgs.begin(), extraArgs.end());
        if (llvmRetTy->isVoidTy())
          builder_.CreateCall(directFn, args);
        else
          result = builder_.CreateCall(directFn, args, "tt.call." + cn);
      }
      builder_.CreateBr(mergeBB);
      results.push_back({builder_.GetInsertBlock(), result});

      if (i + 1 < implClasses.size())
        builder_.SetInsertPoint(nextBB);
    }

    // Default block: no match (shouldn't happen, but need valid IR)
    builder_.SetInsertPoint(defaultBB);
    builder_.CreateBr(mergeBB);

    builder_.SetInsertPoint(mergeBB);
    if (!llvmRetTy->isVoidTy()) {
      phi = builder_.CreatePHI(llvmRetTy, results.size() + 1, "tt.result");
      for (auto &r : results) {
        phi->addIncoming(r.val ? r.val : llvm::Constant::getNullValue(llvmRetTy), r.bb);
      }
      phi->addIncoming(llvm::Constant::getNullValue(llvmRetTy), defaultBB);
      return phi;
    }
    return builder_.getInt32(0);
  }

  // Should not reach here (monomorphize doesn't use emitInterfaceDispatch)
  return builder_.getInt32(0);
}

llvm::Value *Codegen::emitExpr(const Expr &expr) {
  if (expr.kind == ExprKind::IntLiteral) {
    auto &lit = static_cast<const IntLiteralExpr &>(expr);
    return builder_.getInt32(lit.value);
  }
  if (expr.kind == ExprKind::FloatLiteral) {
    auto &lit = static_cast<const FloatLiteralExpr &>(expr);
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), lit.value);
  }
  if (expr.kind == ExprKind::BoolLiteral) {
    auto &lit = static_cast<const BoolLiteralExpr &>(expr);
    return builder_.getInt1(lit.value);
  }
  if (expr.kind == ExprKind::StringLiteral) {
    auto &str = static_cast<const StringLiteralExpr &>(expr);
    return getOrCreateFmtString(str.value, "str");
  }
  if (expr.kind == ExprKind::VarRef) {
    auto &ref = static_cast<const VarRefExpr &>(expr);
    auto it = namedValues_.find(ref.name);
    if (it == namedValues_.end()) {
      llvm::errs() << "Undefined variable: $" << ref.name << "\n";
      return builder_.getInt32(0);
    }
    return builder_.CreateLoad(getLLVMType(it->second.second),
                               it->second.first, ref.name);
  }
  if (expr.kind == ExprKind::FuncCall) {
    auto &call = static_cast<const FuncCallExpr &>(expr);

    // Builtin function dispatch
    if (call.name == "strlen") {
      auto *arg = emitExpr(*call.args[0]);
      return builder_.CreateCall(module_->getFunction("hack_strlen"), {arg}, "strlen");
    }
    if (call.name == "substr") {
      auto *s = emitExpr(*call.args[0]);
      auto *start = emitExpr(*call.args[1]);
      auto *len = emitExpr(*call.args[2]);
      return builder_.CreateCall(module_->getFunction("hack_substr"), {s, start, len}, "substr");
    }
    if (call.name == "intval") {
      auto *arg = emitExpr(*call.args[0]);
      return builder_.CreateCall(module_->getFunction("hack_intval"), {arg}, "intval");
    }
    if (call.name == "str_repeat") {
      auto *s = emitExpr(*call.args[0]);
      auto *n = emitExpr(*call.args[1]);
      return builder_.CreateCall(module_->getFunction("hack_str_repeat"), {s, n}, "str_repeat");
    }
    if (call.name == "count") {
      auto *arg = emitExpr(*call.args[0]);
      HackType argType = inferType(*call.args[0]);
      if (argType == HackType::Dict)
        return builder_.CreateCall(module_->getFunction("hack_dict_size"), {arg}, "count");
      return builder_.CreateCall(module_->getFunction("hack_vec_size"), {arg}, "count");
    }
    if (call.name == "print_r") {
      auto *arg = emitExpr(*call.args[0]);
      HackType argType = inferType(*call.args[0]);
      if (argType == HackType::Dict)
        return builder_.CreateCall(module_->getFunction("hack_print_r_dict"), {arg});
      builder_.CreateCall(module_->getFunction("hack_print_r_vec"), {arg});
      return builder_.getInt32(0);
    }

    // Check if this is a call to an impl block function
    auto stratIt = funcImplStrategy_.find(call.name);
    if (stratIt != funcImplStrategy_.end()) {
      DispatchStrategy strat = stratIt->second;

      if (strat == DispatchStrategy::FatPointer) {
        // Expand interface params: for each interface param, pass (obj, vtable_from_obj[0])
        auto *ptrTy = llvm::PointerType::getUnqual(context_);
        auto *func = module_->getFunction(call.name);
        if (!func) {
          llvm::errs() << "Undefined function: " << call.name << "\n";
          return builder_.getInt32(0);
        }
        std::vector<llvm::Value *> argValues;
        auto typeNamesIt = funcParamTypeNames_.find(call.name);
        for (size_t i = 0; i < call.args.size(); ++i) {
          auto *val = emitExpr(*call.args[i]);
          bool isIfaceParam = false;
          if (typeNamesIt != funcParamTypeNames_.end() && i < typeNamesIt->second.size()) {
            auto ifIt = interfaces_.find(typeNamesIt->second[i]);
            if (ifIt != interfaces_.end()) {
              isIfaceParam = true;
              // Pass data pointer
              argValues.push_back(val);
              // Extract vtable from obj[0]
              std::vector<llvm::Type *> slotTypes = {ptrTy};
              auto *slotStructTy = llvm::StructType::get(context_, slotTypes);
              auto *vtableSlot = plainStructGEP(
                  slotStructTy, val, 0, "fp.extract.vt.slot");
              auto *vtable = builder_.CreateLoad(ptrTy, vtableSlot, "fp.extract.vt");
              argValues.push_back(vtable);
            }
          }
          if (!isIfaceParam) {
            argValues.push_back(val);
          }
        }
        if (func->getReturnType()->isVoidTy())
          return builder_.CreateCall(func, argValues);
        return builder_.CreateCall(func, argValues, "calltmp");
      }

      if (strat == DispatchStrategy::Monomorphize) {
        // Call funcName.ConcreteClass based on varClassName_
        auto typeNamesIt = funcParamTypeNames_.find(call.name);
        std::string suffix;
        for (size_t i = 0; i < call.args.size(); ++i) {
          if (typeNamesIt != funcParamTypeNames_.end() && i < typeNamesIt->second.size()) {
            auto ifIt = interfaces_.find(typeNamesIt->second[i]);
            if (ifIt != interfaces_.end() && call.args[i]->kind == ExprKind::VarRef) {
              auto &ref = static_cast<const VarRefExpr &>(*call.args[i]);
              auto cnIt = varClassName_.find(ref.name);
              if (cnIt != varClassName_.end()) {
                suffix = "." + cnIt->second;
              }
            }
          }
        }
        std::string monoName = call.name + suffix;
        auto *func = module_->getFunction(monoName);
        if (!func) {
          llvm::errs() << "Undefined monomorphized function: " << monoName << "\n";
          return builder_.getInt32(0);
        }
        std::vector<llvm::Value *> argValues;
        for (size_t i = 0; i < call.args.size(); ++i) {
          argValues.push_back(emitExpr(*call.args[i]));
        }
        if (func->getReturnType()->isVoidTy())
          return builder_.CreateCall(func, argValues);
        return builder_.CreateCall(func, argValues, "calltmp");
      }
    }

    auto *func = module_->getFunction(call.name);
    if (!func) {
      llvm::errs() << "Undefined function: " << call.name << "\n";
      return builder_.getInt32(0);
    }
    std::vector<llvm::Value *> argValues;
    for (size_t i = 0; i < call.args.size(); ++i) {
      auto *val = emitExpr(*call.args[i]);
      auto paramIt = funcParamTypes_.find(call.name);
      if (paramIt != funcParamTypes_.end() && i < paramIt->second.size()) {
        HackType paramType = paramIt->second[i];
        HackType argType = inferType(*call.args[i]);
        if (paramType == HackType::Float && argType == HackType::Int)
          val = builder_.CreateSIToFP(val, llvm::Type::getDoubleTy(context_), "itof");
        else if (paramType == HackType::Int && argType == HackType::Float)
          val = builder_.CreateFPToSI(val, llvm::Type::getInt32Ty(context_), "ftoi");
      }
      argValues.push_back(val);
    }
    if (func->getReturnType()->isVoidTy())
      return builder_.CreateCall(func, argValues);
    return builder_.CreateCall(func, argValues, "calltmp");
  }
  if (expr.kind == ExprKind::VecLiteral) {
    auto &vec = static_cast<const VecLiteralExpr &>(expr);
    auto *vecNew = module_->getFunction("hack_vec_new");
    auto *vecPush = module_->getFunction("hack_vec_push");
    auto *v = builder_.CreateCall(vecNew, {}, "vec");
    for (auto &el : vec.elements) {
      auto *val = emitExpr(*el);
      builder_.CreateCall(vecPush, {v, val});
    }
    return v;
  }
  if (expr.kind == ExprKind::DictLiteral) {
    auto &dict = static_cast<const DictLiteralExpr &>(expr);
    auto *dictNew = module_->getFunction("hack_dict_new");
    auto *dictSet = module_->getFunction("hack_dict_set");
    auto *d = builder_.CreateCall(dictNew, {}, "dict");
    for (auto &p : dict.pairs) {
      auto *key = emitExpr(*p.first);
      auto *val = emitExpr(*p.second);
      builder_.CreateCall(dictSet, {d, key, val});
    }
    return d;
  }
  if (expr.kind == ExprKind::Subscript) {
    auto &sub = static_cast<const SubscriptExpr &>(expr);
    auto *obj = emitExpr(*sub.object);
    auto *idx = emitExpr(*sub.index);
    HackType objType = inferType(*sub.object);
    if (objType == HackType::Dict) {
      return builder_.CreateCall(module_->getFunction("hack_dict_get"), {obj, idx}, "dictget");
    }
    return builder_.CreateCall(module_->getFunction("hack_vec_get"), {obj, idx}, "vecget");
  }
  if (expr.kind == ExprKind::New) {
    auto &newExpr = static_cast<const NewExpr &>(expr);
    auto it = classTypes_.find(newExpr.className);
    if (it == classTypes_.end()) {
      llvm::errs() << "Unknown class: " << newExpr.className << "\n";
      return llvm::Constant::getNullValue(llvm::PointerType::getUnqual(context_));
    }
    auto &info = it->second;
    auto *structTy = info.structType;
    auto *dl = &module_->getDataLayout();
    uint64_t size = dl->getTypeAllocSize(structTy);
    auto *mallocFn = module_->getFunction("malloc");
    auto *mem = builder_.CreateCall(mallocFn, {builder_.getInt64(size)}, "obj");

    // If implements interface, store vtable pointer at index 0
    if (info.implementsInterface && info.vtableGlobal) {
      auto *vtSlot = plainStructGEP(structTy, mem, 0, "vt.slot");
      builder_.CreateStore(info.vtableGlobal, vtSlot);
    }

    // Zero-initialize fields (starting from fieldOffset)
    for (size_t i = 0; i < info.fields.size(); ++i) {
      auto *gep = plainStructGEP(structTy, mem, info.fieldOffset + i, info.fields[i].name);
      builder_.CreateStore(llvm::Constant::getNullValue(getLLVMType(info.fields[i].type)), gep);
    }

    // Call __construct
    std::string ctorName = newExpr.className + ".__construct";
    auto *ctorFunc = module_->getFunction(ctorName);
    if (ctorFunc) {
      std::vector<llvm::Value *> args;
      args.push_back(mem);
      for (auto &a : newExpr.args) {
        args.push_back(emitExpr(*a));
      }
      builder_.CreateCall(ctorFunc, args);
    }
    return mem;
  }
  if (expr.kind == ExprKind::MemberAccess) {
    auto &ma = static_cast<const MemberAccessExpr &>(expr);
    auto *obj = emitExpr(*ma.object);

    // Determine class name from variable
    std::string className;
    if (ma.object->kind == ExprKind::VarRef) {
      auto &ref = static_cast<const VarRefExpr &>(*ma.object);
      if (ref.name == "this") className = currentClassName_;
      else {
        auto cnIt = varClassName_.find(ref.name);
        if (cnIt != varClassName_.end()) className = cnIt->second;
      }
    }

    if (!className.empty()) {
      auto infoIt = classTypes_.find(className);
      if (infoIt != classTypes_.end()) {
        auto &info = infoIt->second;
        for (size_t i = 0; i < info.fields.size(); ++i) {
          if (info.fields[i].name == ma.member) {
            auto *gep = plainStructGEP(info.structType, obj,
                info.fieldOffset + i, ma.member);
            return builder_.CreateLoad(getLLVMType(info.fields[i].type), gep, ma.member + ".val");
          }
        }
      }
    }

    // Fallback: search all classes
    for (auto &[cn, info] : classTypes_) {
      for (size_t i = 0; i < info.fields.size(); ++i) {
        if (info.fields[i].name == ma.member) {
          auto *gep = plainStructGEP(info.structType, obj,
              info.fieldOffset + i, ma.member);
          return builder_.CreateLoad(getLLVMType(info.fields[i].type), gep, ma.member + ".val");
        }
      }
    }
    llvm::errs() << "Unknown member: " << ma.member << "\n";
    return builder_.getInt32(0);
  }
  if (expr.kind == ExprKind::MethodCall) {
    auto &mc = static_cast<const MethodCallExpr &>(expr);
    auto *obj = emitExpr(*mc.object);

    // Collect extra args
    std::vector<llvm::Value *> extraArgs;
    for (auto &a : mc.args) {
      extraArgs.push_back(emitExpr(*a));
    }

    // Check if this is a variable with known concrete class (monomorphize direct call)
    if (mc.object->kind == ExprKind::VarRef) {
      auto &ref = static_cast<const VarRefExpr &>(*mc.object);
      auto cnIt = varClassName_.find(ref.name);
      if (cnIt != varClassName_.end()) {
        // Direct call to concrete method
        std::string funcName = cnIt->second + "." + mc.method;
        auto *func = module_->getFunction(funcName);
        if (func) {
          std::vector<llvm::Value *> args;
          args.push_back(obj);
          args.insert(args.end(), extraArgs.begin(), extraArgs.end());
          if (func->getReturnType()->isVoidTy())
            return builder_.CreateCall(func, args);
          return builder_.CreateCall(func, args, "methodcall");
        }
      }

      // Check if interface variable → dispatch
      auto ifIt = varInterfaceName_.find(ref.name);
      if (ifIt != varInterfaceName_.end()) {
        return emitInterfaceDispatch(ref.name, obj, mc.method, extraArgs, currentImplStrategy_);
      }
    }

    // Fallback: direct method call on known class
    // Try $this first
    if (mc.object->kind == ExprKind::VarRef) {
      auto &ref = static_cast<const VarRefExpr &>(*mc.object);
      if (ref.name == "this" && !currentClassName_.empty()) {
        std::string funcName = currentClassName_ + "." + mc.method;
        auto *func = module_->getFunction(funcName);
        if (func) {
          std::vector<llvm::Value *> args;
          args.push_back(obj);
          args.insert(args.end(), extraArgs.begin(), extraArgs.end());
          if (func->getReturnType()->isVoidTy())
            return builder_.CreateCall(func, args);
          return builder_.CreateCall(func, args, "methodcall");
        }
      }
    }

    // Search all classes
    for (auto &[className, info] : classTypes_) {
      std::string funcName = className + "." + mc.method;
      auto *func = module_->getFunction(funcName);
      if (func) {
        std::vector<llvm::Value *> args;
        args.push_back(obj);
        args.insert(args.end(), extraArgs.begin(), extraArgs.end());
        if (func->getReturnType()->isVoidTy())
          return builder_.CreateCall(func, args);
        return builder_.CreateCall(func, args, "methodcall");
      }
    }
    llvm::errs() << "Unknown method: " << mc.method << "\n";
    return builder_.getInt32(0);
  }
  if (expr.kind == ExprKind::UnaryOp) {
    auto &unary = static_cast<const UnaryExpr &>(expr);
    auto *operand = emitExpr(*unary.operand);
    if (unary.op == UnaryOpKind::Not) {
      if (operand->getType()->isIntegerTy(1))
        return builder_.CreateNot(operand, "not");
      return builder_.CreateICmpEQ(operand, llvm::Constant::getNullValue(operand->getType()), "not");
    }
    if (unary.op == UnaryOpKind::Neg) {
      HackType t = inferType(*unary.operand);
      if (t == HackType::Float)
        return builder_.CreateFNeg(operand, "fneg");
      return builder_.CreateNeg(operand, "neg");
    }
  }
  if (expr.kind == ExprKind::BinaryOp) {
    auto &bin = static_cast<const BinaryExpr &>(expr);

    if (bin.op == BinOp::Concat) {
      auto *lhs = emitExpr(*bin.lhs);
      auto *rhs = emitExpr(*bin.rhs);
      auto strcatFunc = module_->getOrInsertFunction("hack_strcat",
          llvm::FunctionType::get(llvm::PointerType::getUnqual(context_),
              {llvm::PointerType::getUnqual(context_),
               llvm::PointerType::getUnqual(context_)}, false));
      return builder_.CreateCall(strcatFunc, {lhs, rhs}, "concat");
    }

    if (bin.op == BinOp::And || bin.op == BinOp::Or) {
      auto *lhs = emitExpr(*bin.lhs);
      if (!lhs->getType()->isIntegerTy(1))
        lhs = builder_.CreateICmpNE(lhs, llvm::Constant::getNullValue(lhs->getType()), "tobool");

      auto *curFunc = builder_.GetInsertBlock()->getParent();
      auto *rhsBB = llvm::BasicBlock::Create(context_, "rhs", curFunc);
      auto *mergeBB = llvm::BasicBlock::Create(context_, "merge", curFunc);

      auto *lhsBB = builder_.GetInsertBlock();
      if (bin.op == BinOp::And)
        builder_.CreateCondBr(lhs, rhsBB, mergeBB);
      else
        builder_.CreateCondBr(lhs, mergeBB, rhsBB);

      builder_.SetInsertPoint(rhsBB);
      auto *rhs = emitExpr(*bin.rhs);
      if (!rhs->getType()->isIntegerTy(1))
        rhs = builder_.CreateICmpNE(rhs, llvm::Constant::getNullValue(rhs->getType()), "tobool");
      auto *rhsEnd = builder_.GetInsertBlock();
      builder_.CreateBr(mergeBB);

      builder_.SetInsertPoint(mergeBB);
      auto *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2, "logic");
      phi->addIncoming(lhs, lhsBB);
      phi->addIncoming(rhs, rhsEnd);
      return phi;
    }

    auto *lhs = emitExpr(*bin.lhs);
    auto *rhs = emitExpr(*bin.rhs);

    if (bin.op == BinOp::Eq || bin.op == BinOp::Neq ||
        bin.op == BinOp::Lt || bin.op == BinOp::Gt ||
        bin.op == BinOp::Le || bin.op == BinOp::Ge) {
      HackType lt = inferType(*bin.lhs);
      HackType rt = inferType(*bin.rhs);
      bool isFloat = (lt == HackType::Float || rt == HackType::Float);
      if (isFloat) {
        if (lt == HackType::Int)
          lhs = builder_.CreateSIToFP(lhs, llvm::Type::getDoubleTy(context_), "itof");
        if (rt == HackType::Int)
          rhs = builder_.CreateSIToFP(rhs, llvm::Type::getDoubleTy(context_), "itof");
        switch (bin.op) {
        case BinOp::Eq:  return builder_.CreateFCmpOEQ(lhs, rhs, "feq");
        case BinOp::Neq: return builder_.CreateFCmpONE(lhs, rhs, "fne");
        case BinOp::Lt:  return builder_.CreateFCmpOLT(lhs, rhs, "flt");
        case BinOp::Gt:  return builder_.CreateFCmpOGT(lhs, rhs, "fgt");
        case BinOp::Le:  return builder_.CreateFCmpOLE(lhs, rhs, "fle");
        case BinOp::Ge:  return builder_.CreateFCmpOGE(lhs, rhs, "fge");
        default: break;
        }
      } else {
        switch (bin.op) {
        case BinOp::Eq:  return builder_.CreateICmpEQ(lhs, rhs, "eq");
        case BinOp::Neq: return builder_.CreateICmpNE(lhs, rhs, "ne");
        case BinOp::Lt:  return builder_.CreateICmpSLT(lhs, rhs, "lt");
        case BinOp::Gt:  return builder_.CreateICmpSGT(lhs, rhs, "gt");
        case BinOp::Le:  return builder_.CreateICmpSLE(lhs, rhs, "le");
        case BinOp::Ge:  return builder_.CreateICmpSGE(lhs, rhs, "ge");
        default: break;
        }
      }
    }

    HackType t = inferType(expr);
    if (t == HackType::Float) {
      HackType lt = inferType(*bin.lhs);
      HackType rt = inferType(*bin.rhs);
      if (lt == HackType::Int)
        lhs = builder_.CreateSIToFP(lhs, llvm::Type::getDoubleTy(context_), "itof");
      if (rt == HackType::Int)
        rhs = builder_.CreateSIToFP(rhs, llvm::Type::getDoubleTy(context_), "itof");
      switch (bin.op) {
      case BinOp::Add: return builder_.CreateFAdd(lhs, rhs, "fadd");
      case BinOp::Sub: return builder_.CreateFSub(lhs, rhs, "fsub");
      case BinOp::Mul: return builder_.CreateFMul(lhs, rhs, "fmul");
      case BinOp::Div: return builder_.CreateFDiv(lhs, rhs, "fdiv");
      case BinOp::Mod: return builder_.CreateFRem(lhs, rhs, "fmod");
      default: break;
      }
    } else {
      switch (bin.op) {
      case BinOp::Add: return builder_.CreateAdd(lhs, rhs, "add");
      case BinOp::Sub: return builder_.CreateSub(lhs, rhs, "sub");
      case BinOp::Mul: return builder_.CreateMul(lhs, rhs, "mul");
      case BinOp::Div: return builder_.CreateSDiv(lhs, rhs, "div");
      case BinOp::Mod: return builder_.CreateSRem(lhs, rhs, "mod");
      default: break;
      }
    }
  }
  return builder_.getInt32(0);
}

void Codegen::emitBlock(const std::vector<std::unique_ptr<Stmt>> &stmts,
                        const FuncDecl &decl) {
  for (const auto &stmt : stmts) {
    emitStmt(*stmt, decl);
    if (stmt->kind == StmtKind::Return)
      break;
  }
}

void Codegen::emitStmt(const Stmt &stmt, const FuncDecl &decl) {
  auto *printfFunc = module_->getFunction("printf");

  if (stmt.kind == StmtKind::Echo) {
    auto *echoStmt = static_cast<const EchoStmt *>(&stmt);
    auto *val = emitExpr(*echoStmt->value);
    HackType valType = inferType(*echoStmt->value);
    if (valType == HackType::String) {
      auto *fmt = getOrCreateFmtString("%s", "strfmt");
      builder_.CreateCall(printfFunc, {fmt, val});
    } else if (valType == HackType::Float) {
      auto *fmt = getOrCreateFmtString("%f", "fltfmt");
      builder_.CreateCall(printfFunc, {fmt, val});
    } else if (valType == HackType::Bool) {
      auto *fmt = getOrCreateFmtString("%d", "intfmt");
      builder_.CreateCall(printfFunc, {fmt, val});
    } else {
      auto *fmt = getOrCreateFmtString("%d", "intfmt");
      builder_.CreateCall(printfFunc, {fmt, val});
    }
  } else if (stmt.kind == StmtKind::VarDecl) {
    auto *varDecl = static_cast<const VarDeclStmt *>(&stmt);
    HackType vt = inferType(*varDecl->init);

    // Track concrete class name for new expressions
    if (varDecl->init->kind == ExprKind::New) {
      auto &newExpr = static_cast<const NewExpr &>(*varDecl->init);
      varClassName_[varDecl->name] = newExpr.className;
      // Also check if this class implements an interface
      auto cIt = classTypes_.find(newExpr.className);
      if (cIt != classTypes_.end() && cIt->second.implementsInterface) {
        // Don't set varInterfaceName_ here — the variable holds a concrete type
      }
    }

    auto it = namedValues_.find(varDecl->name);
    if (it != namedValues_.end()) {
      auto *initVal = emitExpr(*varDecl->init);
      builder_.CreateStore(initVal, it->second.first);
      it->second.second = vt;
    } else {
      // Insert alloca in the entry block to avoid stack growth in loops
      auto *func = builder_.GetInsertBlock()->getParent();
      llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(),
                                     func->getEntryBlock().begin());
      auto *alloca = entryBuilder.CreateAlloca(getLLVMType(vt), nullptr, varDecl->name);
      auto *initVal = emitExpr(*varDecl->init);
      builder_.CreateStore(initVal, alloca);
      namedValues_[varDecl->name] = {alloca, vt};
    }
  } else if (stmt.kind == StmtKind::Return) {
    auto *ret = static_cast<const ReturnStmt *>(&stmt);
    if (ret->value) {
      builder_.CreateRet(emitExpr(*ret->value));
    } else {
      if (decl.isEntryPoint)
        builder_.CreateRet(builder_.getInt32(0));
      else
        builder_.CreateRetVoid();
    }
  } else if (stmt.kind == StmtKind::If) {
    auto *ifStmt = static_cast<const IfStmt *>(&stmt);
    auto *cond = emitExpr(*ifStmt->condition);
    if (!cond->getType()->isIntegerTy(1))
      cond = builder_.CreateICmpNE(cond, llvm::Constant::getNullValue(cond->getType()), "tobool");

    auto *curFunc = builder_.GetInsertBlock()->getParent();
    auto *thenBB = llvm::BasicBlock::Create(context_, "then", curFunc);
    auto *elseBB = llvm::BasicBlock::Create(context_, "else", curFunc);
    auto *mergeBB = llvm::BasicBlock::Create(context_, "ifcont", curFunc);

    builder_.CreateCondBr(cond, thenBB, elseBB);

    builder_.SetInsertPoint(thenBB);
    emitBlock(ifStmt->thenBody, decl);
    if (!builder_.GetInsertBlock()->getTerminator())
      builder_.CreateBr(mergeBB);

    builder_.SetInsertPoint(elseBB);
    if (!ifStmt->elseBody.empty())
      emitBlock(ifStmt->elseBody, decl);
    if (!builder_.GetInsertBlock()->getTerminator())
      builder_.CreateBr(mergeBB);

    builder_.SetInsertPoint(mergeBB);
  } else if (stmt.kind == StmtKind::While) {
    auto *whileStmt = static_cast<const WhileStmt *>(&stmt);
    auto *curFunc = builder_.GetInsertBlock()->getParent();
    auto *condBB = llvm::BasicBlock::Create(context_, "whilecond", curFunc);
    auto *loopBB = llvm::BasicBlock::Create(context_, "whilebody", curFunc);
    auto *afterBB = llvm::BasicBlock::Create(context_, "whileend", curFunc);

    builder_.CreateBr(condBB);

    builder_.SetInsertPoint(condBB);
    auto *cond = emitExpr(*whileStmt->condition);
    if (!cond->getType()->isIntegerTy(1))
      cond = builder_.CreateICmpNE(cond, llvm::Constant::getNullValue(cond->getType()), "tobool");
    builder_.CreateCondBr(cond, loopBB, afterBB);

    builder_.SetInsertPoint(loopBB);
    emitBlock(whileStmt->body, decl);
    if (!builder_.GetInsertBlock()->getTerminator())
      builder_.CreateBr(condBB);

    builder_.SetInsertPoint(afterBB);
  } else if (stmt.kind == StmtKind::For) {
    auto *forStmt = static_cast<const ForStmt *>(&stmt);
    auto *curFunc = builder_.GetInsertBlock()->getParent();

    if (forStmt->init)
      emitStmt(*forStmt->init, decl);

    auto *condBB = llvm::BasicBlock::Create(context_, "forcond", curFunc);
    auto *loopBB = llvm::BasicBlock::Create(context_, "forbody", curFunc);
    auto *updateBB = llvm::BasicBlock::Create(context_, "forupdate", curFunc);
    auto *afterBB = llvm::BasicBlock::Create(context_, "forend", curFunc);

    builder_.CreateBr(condBB);

    builder_.SetInsertPoint(condBB);
    auto *cond = emitExpr(*forStmt->condition);
    if (!cond->getType()->isIntegerTy(1))
      cond = builder_.CreateICmpNE(cond, llvm::Constant::getNullValue(cond->getType()), "tobool");
    builder_.CreateCondBr(cond, loopBB, afterBB);

    builder_.SetInsertPoint(loopBB);
    emitBlock(forStmt->body, decl);
    if (!builder_.GetInsertBlock()->getTerminator())
      builder_.CreateBr(updateBB);

    builder_.SetInsertPoint(updateBB);
    if (forStmt->update)
      emitStmt(*forStmt->update, decl);
    builder_.CreateBr(condBB);

    builder_.SetInsertPoint(afterBB);
  } else if (stmt.kind == StmtKind::Foreach) {
    auto *foreachStmt = static_cast<const ForeachStmt *>(&stmt);
    auto *curFunc = builder_.GetInsertBlock()->getParent();

    auto *iterable = emitExpr(*foreachStmt->iterable);
    HackType iterType = inferType(*foreachStmt->iterable);

    auto *counterAlloca = builder_.CreateAlloca(llvm::Type::getInt32Ty(context_), nullptr, "foreach_i");
    builder_.CreateStore(builder_.getInt32(0), counterAlloca);

    llvm::Value *size;
    if (iterType == HackType::Dict)
      size = builder_.CreateCall(module_->getFunction("hack_dict_size"), {iterable}, "size");
    else
      size = builder_.CreateCall(module_->getFunction("hack_vec_size"), {iterable}, "size");

    if (namedValues_.find(foreachStmt->valueVar) == namedValues_.end()) {
      auto *alloca = builder_.CreateAlloca(llvm::Type::getInt32Ty(context_), nullptr, foreachStmt->valueVar);
      namedValues_[foreachStmt->valueVar] = {alloca, HackType::Int};
    }
    if (iterType == HackType::Dict && !foreachStmt->keyVar.empty()) {
      if (namedValues_.find(foreachStmt->keyVar) == namedValues_.end()) {
        auto *alloca = builder_.CreateAlloca(llvm::PointerType::getUnqual(context_), nullptr, foreachStmt->keyVar);
        namedValues_[foreachStmt->keyVar] = {alloca, HackType::String};
      }
    }

    auto *condBB = llvm::BasicBlock::Create(context_, "foreachcond", curFunc);
    auto *loopBB = llvm::BasicBlock::Create(context_, "foreachbody", curFunc);
    auto *afterBB = llvm::BasicBlock::Create(context_, "foreachend", curFunc);

    builder_.CreateBr(condBB);

    builder_.SetInsertPoint(condBB);
    auto *counter = builder_.CreateLoad(llvm::Type::getInt32Ty(context_), counterAlloca, "i");
    auto *cond = builder_.CreateICmpSLT(counter, size, "cmp");
    builder_.CreateCondBr(cond, loopBB, afterBB);

    builder_.SetInsertPoint(loopBB);
    auto *idx = builder_.CreateLoad(llvm::Type::getInt32Ty(context_), counterAlloca, "idx");

    if (iterType == HackType::Dict) {
      if (!foreachStmt->keyVar.empty()) {
        auto *key = builder_.CreateCall(module_->getFunction("hack_dict_key_at"), {iterable, idx}, "key");
        builder_.CreateStore(key, namedValues_[foreachStmt->keyVar].first);
      }
      auto *val = builder_.CreateCall(module_->getFunction("hack_dict_val_at"), {iterable, idx}, "val");
      builder_.CreateStore(val, namedValues_[foreachStmt->valueVar].first);
    } else {
      auto *val = builder_.CreateCall(module_->getFunction("hack_vec_get"), {iterable, idx}, "val");
      builder_.CreateStore(val, namedValues_[foreachStmt->valueVar].first);
    }

    emitBlock(foreachStmt->body, decl);

    if (!builder_.GetInsertBlock()->getTerminator()) {
      auto *curIdx = builder_.CreateLoad(llvm::Type::getInt32Ty(context_), counterAlloca, "cur");
      auto *next = builder_.CreateAdd(curIdx, builder_.getInt32(1), "next");
      builder_.CreateStore(next, counterAlloca);
      builder_.CreateBr(condBB);
    }

    builder_.SetInsertPoint(afterBB);
  } else if (stmt.kind == StmtKind::SubscriptAssign) {
    auto *sa = static_cast<const SubscriptAssignStmt *>(&stmt);
    auto *obj = emitExpr(*sa->object);
    auto *idx = emitExpr(*sa->index);
    auto *val = emitExpr(*sa->value);
    HackType objType = inferType(*sa->object);
    if (objType == HackType::Dict) {
      builder_.CreateCall(module_->getFunction("hack_dict_set"), {obj, idx, val});
    } else {
      builder_.CreateCall(module_->getFunction("hack_vec_set"), {obj, idx, val});
    }
  } else if (stmt.kind == StmtKind::MemberAssign) {
    auto *ma = static_cast<const MemberAssignStmt *>(&stmt);
    auto *obj = emitExpr(*ma->object);
    auto *val = emitExpr(*ma->value);

    // Determine class
    std::string className;
    if (ma->object->kind == ExprKind::VarRef) {
      auto &ref = static_cast<const VarRefExpr &>(*ma->object);
      if (ref.name == "this") className = currentClassName_;
      else {
        auto cnIt = varClassName_.find(ref.name);
        if (cnIt != varClassName_.end()) className = cnIt->second;
      }
    }

    if (!className.empty()) {
      auto infoIt = classTypes_.find(className);
      if (infoIt != classTypes_.end()) {
        auto &info = infoIt->second;
        for (size_t i = 0; i < info.fields.size(); ++i) {
          if (info.fields[i].name == ma->member) {
            auto *gep = plainStructGEP(info.structType, obj,
                info.fieldOffset + i, ma->member);
            builder_.CreateStore(val, gep);
            return;
          }
        }
      }
    }

    // Fallback
    for (auto &[cn, info] : classTypes_) {
      for (size_t i = 0; i < info.fields.size(); ++i) {
        if (info.fields[i].name == ma->member) {
          auto *gep = plainStructGEP(info.structType, obj,
              info.fieldOffset + i, ma->member);
          builder_.CreateStore(val, gep);
          return;
        }
      }
    }
    llvm::errs() << "Unknown member for assignment: " << ma->member << "\n";
  } else if (stmt.kind == StmtKind::ExprStmt) {
    auto *exprStmt = static_cast<const ExprStmtNode *>(&stmt);
    emitExpr(*exprStmt->expr);
  }
}

void Codegen::emitFunction(const FuncDecl &decl) {
  std::string funcName = decl.isEntryPoint ? "main" : decl.name;

  auto *func = module_->getFunction(funcName);
  if (!func) {
    llvm::errs() << "Function not declared: " << funcName << "\n";
    return;
  }

  auto *entry = llvm::BasicBlock::Create(context_, "entry", func);
  builder_.SetInsertPoint(entry);
  namedValues_.clear();
  varInterfaceName_.clear();
  varClassName_.clear();
  fatPtrVtables_.clear();

  // For impl block functions with fatpointer, params are expanded
  auto stratIt = funcImplStrategy_.find(decl.name);
  bool isFatPointer = (stratIt != funcImplStrategy_.end() &&
                       stratIt->second == DispatchStrategy::FatPointer);

  if (isFatPointer) {
    // Parameters are expanded: interface params become (data, vtable) pairs
    auto typeNamesIt = funcParamTypeNames_.find(decl.name);
    size_t argIdx = 0;
    for (size_t i = 0; i < decl.params.size(); ++i) {
      auto &param = decl.params[i];
      bool isIfaceParam = false;
      if (typeNamesIt != funcParamTypeNames_.end() && i < typeNamesIt->second.size()) {
        auto ifIt = interfaces_.find(typeNamesIt->second[i]);
        if (ifIt != interfaces_.end()) isIfaceParam = true;
      }
      if (isIfaceParam) {
        // Two args: data ptr and vtable ptr
        auto *dataArg = func->getArg(argIdx);
        auto *vtableArg = func->getArg(argIdx + 1);
        dataArg->setName(param.name + ".data");
        vtableArg->setName(param.name + ".vtable");

        auto *dataAlloca = builder_.CreateAlloca(llvm::PointerType::getUnqual(context_), nullptr, param.name);
        builder_.CreateStore(dataArg, dataAlloca);
        namedValues_[param.name] = {dataAlloca, HackType::Object};

        auto *vtAlloca = builder_.CreateAlloca(llvm::PointerType::getUnqual(context_), nullptr, param.name + ".vt");
        builder_.CreateStore(vtableArg, vtAlloca);
        fatPtrVtables_[param.name] = vtAlloca;

        varInterfaceName_[param.name] = typeNamesIt->second[i];
        argIdx += 2;
      } else {
        auto *arg = func->getArg(argIdx);
        arg->setName(param.name);
        auto *alloca = builder_.CreateAlloca(getLLVMType(param.type), nullptr, param.name);
        builder_.CreateStore(arg, alloca);
        namedValues_[param.name] = {alloca, param.type};
        argIdx++;
      }
    }
  } else {
    // Normal parameter handling
    size_t idx = 0;
    auto typeNamesIt = funcParamTypeNames_.find(decl.name);
    for (auto &arg : func->args()) {
      auto &param = decl.params[idx];
      auto *alloca = builder_.CreateAlloca(getLLVMType(param.type), nullptr, param.name);
      builder_.CreateStore(&arg, alloca);
      namedValues_[param.name] = {alloca, param.type};
      arg.setName(param.name);

      // Track interface params
      if (typeNamesIt != funcParamTypeNames_.end() && idx < typeNamesIt->second.size()) {
        auto ifIt = interfaces_.find(typeNamesIt->second[idx]);
        if (ifIt != interfaces_.end()) {
          varInterfaceName_[param.name] = typeNamesIt->second[idx];
        }
      }
      ++idx;
    }
  }

  emitBlock(decl.body, decl);

  if (!builder_.GetInsertBlock()->getTerminator()) {
    if (decl.isEntryPoint)
      builder_.CreateRet(builder_.getInt32(0));
    else if (decl.hackReturnType == HackType::Void)
      builder_.CreateRetVoid();
    else
      builder_.CreateRet(llvm::Constant::getNullValue(
          getLLVMType(decl.hackReturnType)));
  }

  if (llvm::verifyFunction(*func, &llvm::errs())) {
    llvm::errs() << "Verification failed for function: " << funcName << "\n";
  }
}

void Codegen::compile(const Program &prog) {
  declareRuntimeFunctions();

  auto *ptrTy = llvm::PointerType::getUnqual(context_);

  // Store interface info
  for (const auto &iface : prog.interfaces) {
    InterfaceInfo info;
    for (const auto &m : iface.methods) {
      InterfaceInfo::MethodSig sig;
      sig.name = m.name;
      sig.returnType = m.returnType;
      sig.params = m.params;
      info.methods.push_back(std::move(sig));
    }
    interfaces_[iface.name] = std::move(info);
  }

  // Emit class declarations
  for (const auto &cls : prog.classes) {
    emitClassDecl(cls);
  }

  // Declare impl block functions
  for (const auto &impl : prog.implBlocks) {
    for (const auto &decl : impl.functions) {
      std::string funcName = decl.name;
      llvm::Type *retTy = getLLVMType(decl.hackReturnType);

      std::vector<llvm::Type *> paramTypes;
      std::vector<HackType> paramHackTypes;
      std::vector<std::string> paramTypeNames;
      for (const auto &param : decl.params) {
        paramTypeNames.push_back(param.typeName);
        auto ifIt = interfaces_.find(param.typeName);
        if (ifIt != interfaces_.end() && impl.strategy == DispatchStrategy::FatPointer) {
          // Expand to (data_ptr, vtable_ptr)
          paramTypes.push_back(ptrTy);
          paramTypes.push_back(ptrTy);
          paramHackTypes.push_back(HackType::Object);
          paramHackTypes.push_back(HackType::Object);
        } else {
          paramTypes.push_back(getLLVMType(param.type));
          paramHackTypes.push_back(param.type);
        }
      }

      auto *funcType = llvm::FunctionType::get(retTy, paramTypes, false);
      llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                             funcName, module_.get());
      funcTypes_[decl.name] = decl.hackReturnType;
      funcParamTypes_[decl.name] = std::move(paramHackTypes);
      funcImplStrategy_[decl.name] = impl.strategy;
      funcParamTypeNames_[decl.name] = std::move(paramTypeNames);
    }
  }

  // Declare regular functions
  for (const auto &decl : prog.functions) {
    std::string funcName = decl.isEntryPoint ? "main" : decl.name;

    llvm::Type *retTy;
    if (decl.isEntryPoint) {
      retTy = llvm::Type::getInt32Ty(context_);
    } else {
      retTy = getLLVMType(decl.hackReturnType);
    }

    std::vector<llvm::Type *> paramTypes;
    std::vector<HackType> paramHackTypes;
    for (const auto &param : decl.params) {
      paramTypes.push_back(getLLVMType(param.type));
      paramHackTypes.push_back(param.type);
    }

    auto *funcType = llvm::FunctionType::get(retTy, paramTypes, false);
    llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                           funcName, module_.get());
    funcTypes_[decl.name] = decl.hackReturnType;
    funcParamTypes_[decl.name] = std::move(paramHackTypes);
  }

  // Generate monomorphized function copies
  for (const auto &impl : prog.implBlocks) {
    if (impl.strategy != DispatchStrategy::Monomorphize) continue;
    for (const auto &decl : impl.functions) {
      auto typeNamesIt = funcParamTypeNames_.find(decl.name);
      if (typeNamesIt == funcParamTypeNames_.end()) continue;

      // Find interface params and generate a copy for each implementing class
      for (size_t pi = 0; pi < decl.params.size(); ++pi) {
        if (pi >= typeNamesIt->second.size()) continue;
        auto ifIt = interfaces_.find(typeNamesIt->second[pi]);
        if (ifIt == interfaces_.end()) continue;

        // For each class implementing this interface
        for (auto &[className, classInfo] : classTypes_) {
          if (!classInfo.implementsInterface || classInfo.interfaceName != ifIt->first) continue;

          std::string monoName = decl.name + "." + className;
          // Same signature as original but with concrete type
          llvm::Type *retTy = getLLVMType(decl.hackReturnType);
          std::vector<llvm::Type *> paramTypes;
          for (const auto &param : decl.params) {
            paramTypes.push_back(getLLVMType(param.type));
          }
          auto *funcType = llvm::FunctionType::get(retTy, paramTypes, false);
          llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                                 monoName, module_.get());
          funcTypes_[monoName] = decl.hackReturnType;
        }
      }
    }
  }

  // Generate vtable globals (now that all methods are declared)
  for (auto &[className, info] : classTypes_) {
    if (!info.implementsInterface) continue;
    auto ifIt = interfaces_.find(info.interfaceName);
    if (ifIt == interfaces_.end()) continue;

    auto &ifaceInfo = ifIt->second;
    std::vector<llvm::Constant *> vtableEntries;
    for (auto &method : ifaceInfo.methods) {
      std::string funcName = className + "." + method.name;
      auto *func = module_->getFunction(funcName);
      if (func)
        vtableEntries.push_back(func);
      else
        vtableEntries.push_back(llvm::Constant::getNullValue(ptrTy));
    }

    auto *vtableArrayTy = llvm::ArrayType::get(ptrTy, vtableEntries.size());
    auto *vtableConst = llvm::ConstantArray::get(vtableArrayTy, vtableEntries);
    auto *vtableGlobal = new llvm::GlobalVariable(
        *module_, vtableArrayTy, true, llvm::GlobalValue::PrivateLinkage,
        vtableConst, className + "." + info.interfaceName + ".vtable");
    info.vtableGlobal = vtableGlobal;
  }

  // Emit method bodies
  for (const auto &cls : prog.classes) {
    for (const auto &method : cls.methods) {
      std::string funcName = cls.name + "." + method.name;
      auto *func = module_->getFunction(funcName);
      if (!func) continue;

      auto *entry = llvm::BasicBlock::Create(context_, "entry", func);
      builder_.SetInsertPoint(entry);
      namedValues_.clear();
      varInterfaceName_.clear();
      varClassName_.clear();
      fatPtrVtables_.clear();
      currentClassName_ = cls.name;

      auto *argIt = func->arg_begin();
      auto *thisAlloca = builder_.CreateAlloca(ptrTy, nullptr, "this");
      builder_.CreateStore(&*argIt, thisAlloca);
      namedValues_["this"] = {thisAlloca, HackType::Object};
      argIt->setName("this");
      ++argIt;

      size_t idx = 0;
      for (; argIt != func->arg_end(); ++argIt, ++idx) {
        auto &param = method.params[idx];
        auto *alloca = builder_.CreateAlloca(getLLVMType(param.type), nullptr, param.name);
        builder_.CreateStore(&*argIt, alloca);
        namedValues_[param.name] = {alloca, param.type};
        argIt->setName(param.name);
      }

      emitBlock(method.body, method);

      if (!builder_.GetInsertBlock()->getTerminator()) {
        if (method.hackReturnType == HackType::Void)
          builder_.CreateRetVoid();
        else
          builder_.CreateRet(llvm::Constant::getNullValue(
              getLLVMType(method.hackReturnType)));
      }

      if (llvm::verifyFunction(*func, &llvm::errs())) {
        llvm::errs() << "Verification failed for method: " << funcName << "\n";
      }
      currentClassName_.clear();
    }
  }

  // Emit impl block function bodies
  for (const auto &impl : prog.implBlocks) {
    currentImplStrategy_ = impl.strategy;
    for (const auto &decl : impl.functions) {
      emitFunction(decl);
    }

    // For monomorphize, also emit the specialized copies
    if (impl.strategy == DispatchStrategy::Monomorphize) {
      for (const auto &decl : impl.functions) {
        auto typeNamesIt = funcParamTypeNames_.find(decl.name);
        if (typeNamesIt == funcParamTypeNames_.end()) continue;

        for (size_t pi = 0; pi < decl.params.size(); ++pi) {
          if (pi >= typeNamesIt->second.size()) continue;
          auto ifIt = interfaces_.find(typeNamesIt->second[pi]);
          if (ifIt == interfaces_.end()) continue;

          for (auto &[className, classInfo] : classTypes_) {
            if (!classInfo.implementsInterface || classInfo.interfaceName != ifIt->first) continue;

            std::string monoName = decl.name + "." + className;
            auto *func = module_->getFunction(monoName);
            if (!func) continue;

            auto *entry = llvm::BasicBlock::Create(context_, "entry", func);
            builder_.SetInsertPoint(entry);
            namedValues_.clear();
            varInterfaceName_.clear();
            varClassName_.clear();
            fatPtrVtables_.clear();

            // Set up params — interface param gets concrete class name
            size_t argIdx = 0;
            for (size_t i = 0; i < decl.params.size(); ++i) {
              auto &param = decl.params[i];
              auto *arg = func->getArg(argIdx);
              arg->setName(param.name);
              auto *alloca = builder_.CreateAlloca(getLLVMType(param.type), nullptr, param.name);
              builder_.CreateStore(arg, alloca);
              namedValues_[param.name] = {alloca, param.type};

              // If this is the interface param, set concrete class
              if (i == pi) {
                varClassName_[param.name] = className;
              }
              argIdx++;
            }

            emitBlock(decl.body, decl);

            if (!builder_.GetInsertBlock()->getTerminator()) {
              if (decl.hackReturnType == HackType::Void)
                builder_.CreateRetVoid();
              else
                builder_.CreateRet(llvm::Constant::getNullValue(
                    getLLVMType(decl.hackReturnType)));
            }

            if (llvm::verifyFunction(*func, &llvm::errs())) {
              llvm::errs() << "Verification failed for monomorphized: " << monoName << "\n";
            }
          }
        }
      }
    }
  }

  // Emit regular function bodies
  for (const auto &decl : prog.functions) {
    if (decl.isExtern) continue;
    currentImplStrategy_ = DispatchStrategy::Vtable; // default
    emitFunction(decl);
  }
}

void Codegen::dumpIR() const {
  module_->print(llvm::outs(), nullptr);
}

bool Codegen::emitObjectFile(const std::string &path) const {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetAsmPrinter();

  llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  module_->setTargetTriple(triple);

  std::string error;
  auto *target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    llvm::errs() << "Target lookup failed: " << error << "\n";
    return false;
  }

  auto *targetMachine = target->createTargetMachine(
      triple, "generic", "", llvm::TargetOptions(), std::nullopt);
  module_->setDataLayout(targetMachine->createDataLayout());

  std::error_code ec;
  llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    llvm::errs() << "Cannot open output file: " << ec.message() << "\n";
    return false;
  }

  llvm::legacy::PassManager pass;
  if (targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                         llvm::CodeGenFileType::ObjectFile)) {
    llvm::errs() << "Target machine cannot emit object file\n";
    return false;
  }

  pass.run(*module_);
  dest.flush();
  return true;
}
