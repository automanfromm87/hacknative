#include "parser.h"
#include <iostream>
#include <stdexcept>

bool Parser::match(TokenKind kind) {
  if (check(kind)) {
    advance();
    return true;
  }
  return false;
}

void Parser::expect(TokenKind kind, const char *msg) {
  if (!match(kind)) {
    throw std::runtime_error(
        std::string(msg) + " at line " + std::to_string(peek().line) +
        ", got '" + peek().text + "'");
  }
}

std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
  if (check(TokenKind::IntLiteral)) {
    int val = std::stoi(advance().text);
    return std::make_unique<IntLiteralExpr>(val);
  }
  if (check(TokenKind::FloatLiteral)) {
    double val = std::stod(advance().text);
    return std::make_unique<FloatLiteralExpr>(val);
  }
  if (check(TokenKind::KwTrue)) {
    advance();
    return std::make_unique<BoolLiteralExpr>(true);
  }
  if (check(TokenKind::KwFalse)) {
    advance();
    return std::make_unique<BoolLiteralExpr>(false);
  }
  if (check(TokenKind::StringLiteral)) {
    std::string val = advance().text;
    return std::make_unique<StringLiteralExpr>(std::move(val));
  }
  // new ClassName(args)
  if (check(TokenKind::KwNew)) {
    advance();
    if (!check(TokenKind::Identifier))
      throw std::runtime_error("expected class name after 'new'");
    std::string className = advance().text;
    expect(TokenKind::LParen, "expected '(' after class name in new");
    auto newExpr = std::make_unique<NewExpr>(std::move(className));
    if (!check(TokenKind::RParen)) {
      newExpr->args.push_back(parseExpr());
      while (match(TokenKind::Comma)) {
        newExpr->args.push_back(parseExpr());
      }
    }
    expect(TokenKind::RParen, "expected ')' after new arguments");
    return newExpr;
  }
  if (check(TokenKind::Identifier)) {
    std::string name = advance().text;
    // vec[...] literal
    if (name == "vec" && check(TokenKind::LBracket)) {
      advance(); // [
      auto vecExpr = std::make_unique<VecLiteralExpr>();
      if (!check(TokenKind::RBracket)) {
        vecExpr->elements.push_back(parseExpr());
        while (match(TokenKind::Comma)) {
          vecExpr->elements.push_back(parseExpr());
        }
      }
      expect(TokenKind::RBracket, "expected ']' after vec elements");
      return vecExpr;
    }
    // dict[...] literal
    if (name == "dict" && check(TokenKind::LBracket)) {
      advance(); // [
      auto dictExpr = std::make_unique<DictLiteralExpr>();
      if (!check(TokenKind::RBracket)) {
        auto key = parseExpr();
        expect(TokenKind::FatArrow, "expected '=>' in dict literal");
        auto val = parseExpr();
        dictExpr->pairs.emplace_back(std::move(key), std::move(val));
        while (match(TokenKind::Comma)) {
          auto k = parseExpr();
          expect(TokenKind::FatArrow, "expected '=>' in dict literal");
          auto v = parseExpr();
          dictExpr->pairs.emplace_back(std::move(k), std::move(v));
        }
      }
      expect(TokenKind::RBracket, "expected ']' after dict pairs");
      return dictExpr;
    }
    // Function call
    if (check(TokenKind::LParen)) {
      advance(); // (
      auto call = std::make_unique<FuncCallExpr>(std::move(name));
      if (!check(TokenKind::RParen)) {
        call->args.push_back(parseExpr());
        while (match(TokenKind::Comma)) {
          call->args.push_back(parseExpr());
        }
      }
      expect(TokenKind::RParen, "expected ')' after function arguments");
      return call;
    }
    // Bare identifier (shouldn't normally happen in Hack, but fallback)
    throw std::runtime_error(
        "unexpected identifier '" + name + "' at line " +
        std::to_string(peek().line));
  }
  if (check(TokenKind::Variable)) {
    std::string name = advance().text;
    return std::make_unique<VarRefExpr>(std::move(name));
  }
  if (match(TokenKind::LParen)) {
    auto expr = parseExpr();
    expect(TokenKind::RParen, "expected ')'");
    return expr;
  }
  throw std::runtime_error(
      "expected expression at line " + std::to_string(peek().line));
}

// Handle postfix: [index], ->member, ->method()
std::unique_ptr<Expr> Parser::parsePostfixExpr() {
  auto expr = parsePrimaryExpr();
  while (true) {
    if (check(TokenKind::LBracket)) {
      advance(); // [
      auto index = parseExpr();
      expect(TokenKind::RBracket, "expected ']'");
      expr = std::make_unique<SubscriptExpr>(std::move(expr), std::move(index));
    } else if (check(TokenKind::Arrow)) {
      advance(); // ->
      if (!check(TokenKind::Identifier))
        throw std::runtime_error("expected member name after '->'");
      std::string member = advance().text;
      if (check(TokenKind::LParen)) {
        // Method call
        advance(); // (
        auto call = std::make_unique<MethodCallExpr>(std::move(expr), std::move(member));
        if (!check(TokenKind::RParen)) {
          call->args.push_back(parseExpr());
          while (match(TokenKind::Comma)) {
            call->args.push_back(parseExpr());
          }
        }
        expect(TokenKind::RParen, "expected ')' after method arguments");
        expr = std::move(call);
      } else {
        // Member access
        expr = std::make_unique<MemberAccessExpr>(std::move(expr), std::move(member));
      }
    } else {
      break;
    }
  }
  return expr;
}

std::unique_ptr<Expr> Parser::parseUnaryExpr() {
  if (match(TokenKind::Bang)) {
    auto operand = parseUnaryExpr();
    return std::make_unique<UnaryExpr>(UnaryOpKind::Not, std::move(operand));
  }
  if (check(TokenKind::Minus)) {
    advance();
    auto operand = parseUnaryExpr();
    return std::make_unique<UnaryExpr>(UnaryOpKind::Neg, std::move(operand));
  }
  return parsePostfixExpr();
}

std::unique_ptr<Expr> Parser::parseMulExpr() {
  auto lhs = parseUnaryExpr();
  while (check(TokenKind::Star) || check(TokenKind::Slash) ||
         check(TokenKind::Percent)) {
    BinOp op;
    if (match(TokenKind::Star))
      op = BinOp::Mul;
    else if (match(TokenKind::Slash))
      op = BinOp::Div;
    else {
      advance();
      op = BinOp::Mod;
    }
    auto rhs = parseUnaryExpr();
    lhs = std::make_unique<BinaryExpr>(op, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseConcatExpr() {
  auto lhs = parseMulExpr();
  while (check(TokenKind::Dot)) {
    advance();
    auto rhs = parseMulExpr();
    lhs = std::make_unique<BinaryExpr>(BinOp::Concat, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseAddExpr() {
  auto lhs = parseConcatExpr();
  while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
    BinOp op = match(TokenKind::Plus) ? BinOp::Add : (advance(), BinOp::Sub);
    auto rhs = parseConcatExpr();
    lhs = std::make_unique<BinaryExpr>(op, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseCompareExpr() {
  auto lhs = parseAddExpr();
  while (check(TokenKind::EqEq) || check(TokenKind::BangEq) ||
         check(TokenKind::LAngle) || check(TokenKind::RAngle) ||
         check(TokenKind::LessEq) || check(TokenKind::GreaterEq)) {
    BinOp op;
    if (match(TokenKind::EqEq)) op = BinOp::Eq;
    else if (match(TokenKind::BangEq)) op = BinOp::Neq;
    else if (match(TokenKind::LAngle)) op = BinOp::Lt;
    else if (match(TokenKind::RAngle)) op = BinOp::Gt;
    else if (match(TokenKind::LessEq)) op = BinOp::Le;
    else { advance(); op = BinOp::Ge; }
    auto rhs = parseAddExpr();
    lhs = std::make_unique<BinaryExpr>(op, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseAndExpr() {
  auto lhs = parseCompareExpr();
  while (match(TokenKind::AmpAmp)) {
    auto rhs = parseCompareExpr();
    lhs = std::make_unique<BinaryExpr>(BinOp::And, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseOrExpr() {
  auto lhs = parseAndExpr();
  while (match(TokenKind::PipePipe)) {
    auto rhs = parseAndExpr();
    lhs = std::make_unique<BinaryExpr>(BinOp::Or, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseExpr() { return parseOrExpr(); }

std::vector<std::unique_ptr<Stmt>> Parser::parseBlock() {
  expect(TokenKind::LBrace, "expected '{'");
  std::vector<std::unique_ptr<Stmt>> stmts;
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    stmts.push_back(parseStmt());
  }
  expect(TokenKind::RBrace, "expected '}'");
  return stmts;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
  if (match(TokenKind::KwReturn)) {
    std::unique_ptr<Expr> value;
    if (!check(TokenKind::Semicolon)) {
      value = parseExpr();
    }
    expect(TokenKind::Semicolon, "expected ';' after return");
    return std::make_unique<ReturnStmt>(std::move(value));
  }
  if (match(TokenKind::KwEcho)) {
    auto value = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' after echo");
    return std::make_unique<EchoStmt>(std::move(value));
  }
  if (match(TokenKind::KwIf)) {
    auto ifStmt = std::make_unique<IfStmt>();
    expect(TokenKind::LParen, "expected '(' after 'if'");
    ifStmt->condition = parseExpr();
    expect(TokenKind::RParen, "expected ')' after if condition");
    ifStmt->thenBody = parseBlock();
    if (match(TokenKind::KwElse)) {
      if (check(TokenKind::KwIf)) {
        ifStmt->elseBody.push_back(parseStmt());
      } else {
        ifStmt->elseBody = parseBlock();
      }
    }
    return ifStmt;
  }
  if (match(TokenKind::KwWhile)) {
    auto whileStmt = std::make_unique<WhileStmt>();
    expect(TokenKind::LParen, "expected '(' after 'while'");
    whileStmt->condition = parseExpr();
    expect(TokenKind::RParen, "expected ')' after while condition");
    whileStmt->body = parseBlock();
    return whileStmt;
  }
  if (match(TokenKind::KwFor)) {
    auto forStmt = std::make_unique<ForStmt>();
    expect(TokenKind::LParen, "expected '(' after 'for'");
    if (check(TokenKind::Variable)) {
      std::string name = advance().text;
      expect(TokenKind::Equals, "expected '=' in for init");
      auto init = parseExpr();
      forStmt->init = std::make_unique<VarDeclStmt>(std::move(name), std::move(init));
    }
    expect(TokenKind::Semicolon, "expected ';' after for init");
    forStmt->condition = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' after for condition");
    if (check(TokenKind::Variable)) {
      std::string name = advance().text;
      expect(TokenKind::Equals, "expected '=' in for update");
      auto upd = parseExpr();
      forStmt->update = std::make_unique<VarDeclStmt>(std::move(name), std::move(upd));
    }
    expect(TokenKind::RParen, "expected ')' after for clauses");
    forStmt->body = parseBlock();
    return forStmt;
  }
  // foreach ($iterable as $val) or foreach ($iterable as $key => $val)
  if (match(TokenKind::KwForeach)) {
    auto foreachStmt = std::make_unique<ForeachStmt>();
    expect(TokenKind::LParen, "expected '(' after 'foreach'");
    foreachStmt->iterable = parseExpr();
    expect(TokenKind::KwAs, "expected 'as' in foreach");
    if (!check(TokenKind::Variable))
      throw std::runtime_error("expected variable in foreach");
    std::string firstVar = advance().text;
    if (match(TokenKind::FatArrow)) {
      // key => value form
      foreachStmt->keyVar = std::move(firstVar);
      if (!check(TokenKind::Variable))
        throw std::runtime_error("expected value variable in foreach");
      foreachStmt->valueVar = advance().text;
    } else {
      foreachStmt->valueVar = std::move(firstVar);
    }
    expect(TokenKind::RParen, "expected ')' after foreach");
    foreachStmt->body = parseBlock();
    return foreachStmt;
  }
  if (check(TokenKind::Variable)) {
    // Look ahead: $var = ...; or $var[...] = ...; or $var->member = ...;
    // or expression statement
    size_t saved = pos_;
    std::string name = advance().text;

    // $var->member = val;
    if (check(TokenKind::Arrow)) {
      advance(); // ->
      if (check(TokenKind::Identifier)) {
        std::string member = advance().text;
        if (check(TokenKind::Equals)) {
          advance(); // =
          auto obj = std::make_unique<VarRefExpr>(std::move(name));
          auto val = parseExpr();
          expect(TokenKind::Semicolon, "expected ';' after member assignment");
          return std::make_unique<MemberAssignStmt>(std::move(obj), std::move(member), std::move(val));
        }
      }
      // Not a member assign, backtrack
      pos_ = saved;
    }
    // $var[index] = val;
    else if (check(TokenKind::LBracket)) {
      advance(); // [
      auto index = parseExpr();
      expect(TokenKind::RBracket, "expected ']'");
      if (check(TokenKind::Equals)) {
        advance(); // =
        auto obj = std::make_unique<VarRefExpr>(std::move(name));
        auto val = parseExpr();
        expect(TokenKind::Semicolon, "expected ';' after subscript assignment");
        return std::make_unique<SubscriptAssignStmt>(std::move(obj), std::move(index), std::move(val));
      }
      // Not a subscript assign, backtrack
      pos_ = saved;
    }
    // $var = expr;
    else if (check(TokenKind::Equals)) {
      advance(); // =
      auto init = parseExpr();
      expect(TokenKind::Semicolon, "expected ';' after variable declaration");
      return std::make_unique<VarDeclStmt>(std::move(name), std::move(init));
    }
    // Not an assignment, backtrack and fall through to expression statement
    else {
      pos_ = saved;
    }
  }
  // Expression statement (function call, method call, etc.)
  {
    auto expr = parseExpr();
    expect(TokenKind::Semicolon, "expected ';' after expression statement");
    return std::make_unique<ExprStmtNode>(std::move(expr));
  }
}

HackType parseHackType(const std::string &name) {
  if (name == "int") return HackType::Int;
  if (name == "float") return HackType::Float;
  if (name == "bool") return HackType::Bool;
  if (name == "string") return HackType::String;
  if (name == "void") return HackType::Void;
  // Unknown type name (e.g. interface/class name) → Object
  if (!name.empty() && isupper(name[0])) return HackType::Object;
  return HackType::Void;
}

FuncDecl Parser::parseFuncDecl(bool hasEntryPoint) {
  FuncDecl decl;
  decl.isEntryPoint = hasEntryPoint;

  decl.isAsync = match(TokenKind::KwAsync);

  expect(TokenKind::KwFunction, "expected 'function'");

  if (!check(TokenKind::Identifier))
    throw std::runtime_error("expected function name");
  decl.name = advance().text;

  expect(TokenKind::LParen, "expected '('");
  if (!check(TokenKind::RParen)) {
    do {
      Param param;
      if (!check(TokenKind::Identifier))
        throw std::runtime_error("expected parameter type");
      std::string typeName = advance().text;
      param.typeName = typeName;
      param.type = parseHackType(typeName);
      if (!check(TokenKind::Variable))
        throw std::runtime_error("expected parameter name ($...)");
      param.name = advance().text;
      decl.params.push_back(std::move(param));
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen, "expected ')'");

  if (match(TokenKind::Colon)) {
    if (match(TokenKind::KwAwaitable)) {
      expect(TokenKind::LAngle, "expected '<'");
      if (check(TokenKind::Identifier))
        advance(); // "void"
      expect(TokenKind::RAngle, "expected '>'");
      decl.returnType = "void";
    } else if (check(TokenKind::Identifier)) {
      decl.returnType = advance().text;
    }
  }

  decl.hackReturnType = parseHackType(decl.returnType);

  if (check(TokenKind::Semicolon)) {
    // HHI-style declaration: function name(params): type;
    expect(TokenKind::Semicolon, "expected ';' after function declaration");
    decl.isExtern = true;
  } else {
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
      decl.body.push_back(parseStmt());
    }
    expect(TokenKind::RBrace, "expected '}'");
  }

  return decl;
}

ClassDecl Parser::parseClassDecl() {
  ClassDecl cls;
  expect(TokenKind::KwClass, "expected 'class'");
  if (!check(TokenKind::Identifier))
    throw std::runtime_error("expected class name");
  cls.name = advance().text;

  // Check for "implements InterfaceName"
  if (match(TokenKind::KwImplements)) {
    if (!check(TokenKind::Identifier))
      throw std::runtime_error("expected interface name after 'implements'");
    cls.implementsInterface = advance().text;
  }

  expect(TokenKind::LBrace, "expected '{' after class name");

  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    bool isPublic = true;
    if (match(TokenKind::KwPublic)) {
      isPublic = true;
    } else if (match(TokenKind::KwPrivate)) {
      isPublic = false;
    }

    if (check(TokenKind::KwFunction) || check(TokenKind::KwAsync)) {
      // Method
      FuncDecl method;
      method.isAsync = match(TokenKind::KwAsync);
      expect(TokenKind::KwFunction, "expected 'function'");
      if (!check(TokenKind::Identifier))
        throw std::runtime_error("expected method name");
      method.name = advance().text;
      expect(TokenKind::LParen, "expected '('");
      if (!check(TokenKind::RParen)) {
        do {
          Param param;
          if (!check(TokenKind::Identifier))
            throw std::runtime_error("expected parameter type");
          std::string typeName = advance().text;
          param.typeName = typeName;
          param.type = parseHackType(typeName);
          if (!check(TokenKind::Variable))
            throw std::runtime_error("expected parameter name");
          param.name = advance().text;
          method.params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen, "expected ')'");
      if (match(TokenKind::Colon)) {
        if (check(TokenKind::Identifier)) {
          method.returnType = advance().text;
        }
      }
      method.hackReturnType = parseHackType(method.returnType);
      // Interface method (no body, just semicolon) vs regular method
      if (check(TokenKind::Semicolon)) {
        advance(); // consume ';'
        cls.methods.push_back(std::move(method));
        continue;
      }
      expect(TokenKind::LBrace, "expected '{' for method body");
      while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        method.body.push_back(parseStmt());
      }
      expect(TokenKind::RBrace, "expected '}'");
      cls.methods.push_back(std::move(method));
    } else if (check(TokenKind::Identifier)) {
      // Field: type $name;
      FieldDecl field;
      field.isPublic = isPublic;
      std::string typeName = advance().text;
      field.type = parseHackType(typeName);
      if (!check(TokenKind::Variable))
        throw std::runtime_error("expected field name ($...)");
      field.name = advance().text;
      expect(TokenKind::Semicolon, "expected ';' after field declaration");
      cls.fields.push_back(std::move(field));
    } else {
      throw std::runtime_error("unexpected token in class body: " + peek().text);
    }
  }
  expect(TokenKind::RBrace, "expected '}' after class body");
  return cls;
}

InterfaceDecl Parser::parseInterfaceDecl() {
  InterfaceDecl iface;
  expect(TokenKind::KwInterface, "expected 'interface'");
  if (!check(TokenKind::Identifier))
    throw std::runtime_error("expected interface name");
  iface.name = advance().text;
  expect(TokenKind::LBrace, "expected '{' after interface name");

  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    // public function name(params): returnType;
    match(TokenKind::KwPublic); // optional
    expect(TokenKind::KwFunction, "expected 'function' in interface method");
    InterfaceMethodSig sig;
    if (!check(TokenKind::Identifier))
      throw std::runtime_error("expected method name in interface");
    sig.name = advance().text;
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) {
      do {
        Param param;
        if (!check(TokenKind::Identifier))
          throw std::runtime_error("expected parameter type");
        std::string typeName = advance().text;
        param.typeName = typeName;
        param.type = parseHackType(typeName);
        if (!check(TokenKind::Variable))
          throw std::runtime_error("expected parameter name");
        param.name = advance().text;
        sig.params.push_back(std::move(param));
      } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')'");
    if (match(TokenKind::Colon)) {
      if (check(TokenKind::Identifier)) {
        sig.returnType = parseHackType(advance().text);
      }
    }
    expect(TokenKind::Semicolon, "expected ';' after interface method signature");
    iface.methods.push_back(std::move(sig));
  }
  expect(TokenKind::RBrace, "expected '}' after interface body");
  return iface;
}

ImplBlock Parser::parseImplBlock() {
  ImplBlock block;
  expect(TokenKind::KwImpl, "expected 'impl'");
  if (!check(TokenKind::Identifier))
    throw std::runtime_error("expected dispatch strategy after 'impl'");
  std::string strategy = advance().text;
  if (strategy == "vtable") block.strategy = DispatchStrategy::Vtable;
  else if (strategy == "fatpointer") block.strategy = DispatchStrategy::FatPointer;
  else if (strategy == "typetag") block.strategy = DispatchStrategy::TypeTag;
  else if (strategy == "monomorphize") block.strategy = DispatchStrategy::Monomorphize;
  else throw std::runtime_error("unknown dispatch strategy: " + strategy);

  expect(TokenKind::LBrace, "expected '{' after impl strategy");
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
    block.functions.push_back(parseFuncDecl(false));
  }
  expect(TokenKind::RBrace, "expected '}' after impl block");
  return block;
}

Program Parser::parse() {
  Program prog;

  while (!check(TokenKind::Eof)) {
    bool hasEntryPoint = false;

    if (check(TokenKind::AttrStart)) {
      advance();
      if (check(TokenKind::Underscore2)) {
        advance();
        if (!check(TokenKind::Identifier) || peek().text != "EntryPoint")
          throw std::runtime_error("expected 'EntryPoint' after '__'");
        advance();
      } else if (check(TokenKind::Identifier) && peek().text == "__EntryPoint") {
        advance();
      } else {
        throw std::runtime_error("expected '__EntryPoint'");
      }
      expect(TokenKind::AttrEnd, "expected '>>'");
      hasEntryPoint = true;
    }

    if (check(TokenKind::KwInterface)) {
      prog.interfaces.push_back(parseInterfaceDecl());
    } else if (check(TokenKind::KwImpl)) {
      prog.implBlocks.push_back(parseImplBlock());
    } else if (check(TokenKind::KwClass)) {
      prog.classes.push_back(parseClassDecl());
    } else {
      prog.functions.push_back(parseFuncDecl(hasEntryPoint));
    }
  }

  return prog;
}

static const char *hackTypeName(HackType t) {
  switch (t) {
  case HackType::Int:    return "Int";
  case HackType::Float:  return "Float";
  case HackType::Bool:   return "Bool";
  case HackType::String: return "String";
  case HackType::Void:   return "Void";
  case HackType::Vec:    return "Vec";
  case HackType::Dict:   return "Dict";
  case HackType::Object: return "Object";
  }
  return "Unknown";
}

static const char *binOpName(BinOp op) {
  switch (op) {
  case BinOp::Add: return "Add";
  case BinOp::Sub: return "Sub";
  case BinOp::Mul: return "Mul";
  case BinOp::Div: return "Div";
  case BinOp::Mod: return "Mod";
  case BinOp::Eq:  return "Eq";
  case BinOp::Neq: return "Neq";
  case BinOp::Lt:  return "Lt";
  case BinOp::Gt:  return "Gt";
  case BinOp::Le:  return "Le";
  case BinOp::Ge:  return "Ge";
  case BinOp::And: return "And";
  case BinOp::Or:  return "Or";
  case BinOp::Concat: return "Concat";
  }
  return "?";
}

static const char *unaryOpName(UnaryOpKind op) {
  switch (op) {
  case UnaryOpKind::Not: return "Not";
  case UnaryOpKind::Neg: return "Neg";
  }
  return "?";
}

static void dumpExpr(const Expr &expr, std::ostream &os, int indent) {
  std::string pad(indent, ' ');
  switch (expr.kind) {
  case ExprKind::IntLiteral: {
    auto &e = static_cast<const IntLiteralExpr &>(expr);
    os << pad << "IntLiteral " << e.value << "\n";
    break;
  }
  case ExprKind::FloatLiteral: {
    auto &e = static_cast<const FloatLiteralExpr &>(expr);
    os << pad << "FloatLiteral " << e.value << "\n";
    break;
  }
  case ExprKind::BoolLiteral: {
    auto &e = static_cast<const BoolLiteralExpr &>(expr);
    os << pad << "BoolLiteral " << (e.value ? "true" : "false") << "\n";
    break;
  }
  case ExprKind::StringLiteral: {
    auto &e = static_cast<const StringLiteralExpr &>(expr);
    os << pad << "StringLiteral \"" << e.value << "\"\n";
    break;
  }
  case ExprKind::BinaryOp: {
    auto &e = static_cast<const BinaryExpr &>(expr);
    os << pad << "BinaryExpr " << binOpName(e.op) << "\n";
    dumpExpr(*e.lhs, os, indent + 2);
    dumpExpr(*e.rhs, os, indent + 2);
    break;
  }
  case ExprKind::UnaryOp: {
    auto &e = static_cast<const UnaryExpr &>(expr);
    os << pad << "UnaryExpr " << unaryOpName(e.op) << "\n";
    dumpExpr(*e.operand, os, indent + 2);
    break;
  }
  case ExprKind::VarRef: {
    auto &e = static_cast<const VarRefExpr &>(expr);
    os << pad << "VarRef $" << e.name << "\n";
    break;
  }
  case ExprKind::FuncCall: {
    auto &e = static_cast<const FuncCallExpr &>(expr);
    os << pad << "FuncCall " << e.name << "(...)\n";
    for (const auto &arg : e.args)
      dumpExpr(*arg, os, indent + 2);
    break;
  }
  case ExprKind::VecLiteral: {
    auto &e = static_cast<const VecLiteralExpr &>(expr);
    os << pad << "VecLiteral [" << e.elements.size() << " elements]\n";
    for (const auto &el : e.elements)
      dumpExpr(*el, os, indent + 2);
    break;
  }
  case ExprKind::DictLiteral: {
    auto &e = static_cast<const DictLiteralExpr &>(expr);
    os << pad << "DictLiteral [" << e.pairs.size() << " pairs]\n";
    for (const auto &p : e.pairs) {
      dumpExpr(*p.first, os, indent + 2);
      dumpExpr(*p.second, os, indent + 2);
    }
    break;
  }
  case ExprKind::Subscript: {
    auto &e = static_cast<const SubscriptExpr &>(expr);
    os << pad << "Subscript\n";
    dumpExpr(*e.object, os, indent + 2);
    dumpExpr(*e.index, os, indent + 2);
    break;
  }
  case ExprKind::New: {
    auto &e = static_cast<const NewExpr &>(expr);
    os << pad << "New " << e.className << "(...)\n";
    for (const auto &arg : e.args)
      dumpExpr(*arg, os, indent + 2);
    break;
  }
  case ExprKind::MemberAccess: {
    auto &e = static_cast<const MemberAccessExpr &>(expr);
    os << pad << "MemberAccess ->" << e.member << "\n";
    dumpExpr(*e.object, os, indent + 2);
    break;
  }
  case ExprKind::MethodCall: {
    auto &e = static_cast<const MethodCallExpr &>(expr);
    os << pad << "MethodCall ->" << e.method << "(...)\n";
    dumpExpr(*e.object, os, indent + 2);
    for (const auto &arg : e.args)
      dumpExpr(*arg, os, indent + 2);
    break;
  }
  }
}

static void dumpStmt(const Stmt &stmt, std::ostream &os, int indent);

static void dumpBlock(const std::vector<std::unique_ptr<Stmt>> &stmts,
                      std::ostream &os, int indent) {
  for (const auto &s : stmts) {
    dumpStmt(*s, os, indent);
  }
}

static void dumpStmt(const Stmt &stmt, std::ostream &os, int indent) {
  std::string pad(indent, ' ');
  switch (stmt.kind) {
  case StmtKind::Return: {
    auto &s = static_cast<const ReturnStmt &>(stmt);
    os << pad << "ReturnStmt\n";
    if (s.value)
      dumpExpr(*s.value, os, indent + 2);
    break;
  }
  case StmtKind::VarDecl: {
    auto &s = static_cast<const VarDeclStmt &>(stmt);
    os << pad << "VarDeclStmt $" << s.name << "\n";
    dumpExpr(*s.init, os, indent + 2);
    break;
  }
  case StmtKind::Echo: {
    auto &s = static_cast<const EchoStmt &>(stmt);
    os << pad << "EchoStmt\n";
    dumpExpr(*s.value, os, indent + 2);
    break;
  }
  case StmtKind::If: {
    auto &s = static_cast<const IfStmt &>(stmt);
    os << pad << "IfStmt\n";
    os << pad << "  Condition:\n";
    dumpExpr(*s.condition, os, indent + 4);
    os << pad << "  Then:\n";
    dumpBlock(s.thenBody, os, indent + 4);
    if (!s.elseBody.empty()) {
      os << pad << "  Else:\n";
      dumpBlock(s.elseBody, os, indent + 4);
    }
    break;
  }
  case StmtKind::While: {
    auto &s = static_cast<const WhileStmt &>(stmt);
    os << pad << "WhileStmt\n";
    os << pad << "  Condition:\n";
    dumpExpr(*s.condition, os, indent + 4);
    os << pad << "  Body:\n";
    dumpBlock(s.body, os, indent + 4);
    break;
  }
  case StmtKind::For: {
    auto &s = static_cast<const ForStmt &>(stmt);
    os << pad << "ForStmt\n";
    if (s.init) {
      os << pad << "  Init:\n";
      dumpStmt(*s.init, os, indent + 4);
    }
    os << pad << "  Condition:\n";
    dumpExpr(*s.condition, os, indent + 4);
    if (s.update) {
      os << pad << "  Update:\n";
      dumpStmt(*s.update, os, indent + 4);
    }
    os << pad << "  Body:\n";
    dumpBlock(s.body, os, indent + 4);
    break;
  }
  case StmtKind::ExprStmt: {
    auto &s = static_cast<const ExprStmtNode &>(stmt);
    os << pad << "ExprStmt\n";
    dumpExpr(*s.expr, os, indent + 2);
    break;
  }
  case StmtKind::SubscriptAssign: {
    auto &s = static_cast<const SubscriptAssignStmt &>(stmt);
    os << pad << "SubscriptAssign\n";
    dumpExpr(*s.object, os, indent + 2);
    os << pad << "  Index:\n";
    dumpExpr(*s.index, os, indent + 4);
    os << pad << "  Value:\n";
    dumpExpr(*s.value, os, indent + 4);
    break;
  }
  case StmtKind::Foreach: {
    auto &s = static_cast<const ForeachStmt &>(stmt);
    os << pad << "ForeachStmt";
    if (!s.keyVar.empty())
      os << " $" << s.keyVar << " =>";
    os << " $" << s.valueVar << "\n";
    os << pad << "  Iterable:\n";
    dumpExpr(*s.iterable, os, indent + 4);
    os << pad << "  Body:\n";
    dumpBlock(s.body, os, indent + 4);
    break;
  }
  case StmtKind::MemberAssign: {
    auto &s = static_cast<const MemberAssignStmt &>(stmt);
    os << pad << "MemberAssign ->" << s.member << "\n";
    dumpExpr(*s.object, os, indent + 2);
    os << pad << "  Value:\n";
    dumpExpr(*s.value, os, indent + 4);
    break;
  }
  }
}

static const char *dispatchStrategyName(DispatchStrategy s) {
  switch (s) {
  case DispatchStrategy::Vtable: return "vtable";
  case DispatchStrategy::FatPointer: return "fatpointer";
  case DispatchStrategy::TypeTag: return "typetag";
  case DispatchStrategy::Monomorphize: return "monomorphize";
  }
  return "?";
}

void dumpAST(const Program &prog, std::ostream &os, int indent) {
  os << "Program\n";
  for (const auto &iface : prog.interfaces) {
    os << "  InterfaceDecl \"" << iface.name << "\"\n";
    for (const auto &m : iface.methods) {
      os << "    MethodSig \"" << m.name << "\" -> " << hackTypeName(m.returnType) << "\n";
    }
  }
  for (const auto &cls : prog.classes) {
    os << "  ClassDecl \"" << cls.name << "\"";
    if (!cls.implementsInterface.empty())
      os << " implements " << cls.implementsInterface;
    os << "\n";
    for (const auto &f : cls.fields) {
      os << "    Field " << hackTypeName(f.type) << " $" << f.name
         << (f.isPublic ? " public" : " private") << "\n";
    }
    for (const auto &m : cls.methods) {
      os << "    Method \"" << m.name << "\" -> " << hackTypeName(m.hackReturnType);
      if (!m.params.empty()) {
        os << " (";
        for (size_t i = 0; i < m.params.size(); ++i) {
          if (i > 0) os << ", ";
          os << hackTypeName(m.params[i].type) << " $" << m.params[i].name;
        }
        os << ")";
      }
      os << "\n";
      for (const auto &stmt : m.body)
        dumpStmt(*stmt, os, indent + 6);
    }
  }
  for (const auto &func : prog.functions) {
    os << "  FuncDecl \"" << func.name << "\"";
    if (func.isAsync)
      os << " async";
    if (func.isEntryPoint)
      os << " [EntryPoint]";
    os << " -> " << hackTypeName(func.hackReturnType);
    if (!func.params.empty()) {
      os << " (";
      for (size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) os << ", ";
        os << hackTypeName(func.params[i].type) << " $" << func.params[i].name;
      }
      os << ")";
    }
    os << "\n";
    for (const auto &stmt : func.body) {
      dumpStmt(*stmt, os, indent + 4);
    }
  }
  for (const auto &impl : prog.implBlocks) {
    os << "  ImplBlock [" << dispatchStrategyName(impl.strategy) << "]\n";
    for (const auto &func : impl.functions) {
      os << "    FuncDecl \"" << func.name << "\"";
      os << " -> " << hackTypeName(func.hackReturnType);
      if (!func.params.empty()) {
        os << " (";
        for (size_t i = 0; i < func.params.size(); ++i) {
          if (i > 0) os << ", ";
          os << hackTypeName(func.params[i].type) << " $" << func.params[i].name;
        }
        os << ")";
      }
      os << "\n";
      for (const auto &stmt : func.body) {
        dumpStmt(*stmt, os, indent + 6);
      }
    }
  }
}
