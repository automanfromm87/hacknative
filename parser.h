#pragma once
#include "lexer.h"
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// Type system
enum class HackType { Int, Float, Bool, String, Void, Vec, Dict, Object };

// AST nodes
enum class ExprKind {
  IntLiteral, FloatLiteral, BoolLiteral, StringLiteral,
  BinaryOp, UnaryOp, VarRef, FuncCall,
  VecLiteral, DictLiteral, Subscript,
  New, MemberAccess, MethodCall
};

enum class BinOp { Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, Le, Ge, And, Or, Concat };

enum class UnaryOpKind { Not, Neg };

struct Expr {
  ExprKind kind;
  HackType type = HackType::Int;
  explicit Expr(ExprKind k) : kind(k) {}
  virtual ~Expr() = default;
};

struct IntLiteralExpr : Expr {
  int value;
  explicit IntLiteralExpr(int v) : Expr(ExprKind::IntLiteral), value(v) {
    type = HackType::Int;
  }
};

struct FloatLiteralExpr : Expr {
  double value;
  explicit FloatLiteralExpr(double v) : Expr(ExprKind::FloatLiteral), value(v) {
    type = HackType::Float;
  }
};

struct BoolLiteralExpr : Expr {
  bool value;
  explicit BoolLiteralExpr(bool v) : Expr(ExprKind::BoolLiteral), value(v) {
    type = HackType::Bool;
  }
};

struct BinaryExpr : Expr {
  BinOp op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  BinaryExpr(BinOp op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
      : Expr(ExprKind::BinaryOp), op(op), lhs(std::move(l)),
        rhs(std::move(r)) {}
};

struct UnaryExpr : Expr {
  UnaryOpKind op;
  std::unique_ptr<Expr> operand;
  UnaryExpr(UnaryOpKind op, std::unique_ptr<Expr> operand)
      : Expr(ExprKind::UnaryOp), op(op), operand(std::move(operand)) {}
};

struct StringLiteralExpr : Expr {
  std::string value;
  explicit StringLiteralExpr(std::string v)
      : Expr(ExprKind::StringLiteral), value(std::move(v)) {
    type = HackType::String;
  }
};

struct VarRefExpr : Expr {
  std::string name;
  explicit VarRefExpr(std::string n)
      : Expr(ExprKind::VarRef), name(std::move(n)) {}
};

struct FuncCallExpr : Expr {
  std::string name;
  std::vector<std::unique_ptr<Expr>> args;
  explicit FuncCallExpr(std::string n)
      : Expr(ExprKind::FuncCall), name(std::move(n)) {}
};

// vec[1, 2, 3]
struct VecLiteralExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
  VecLiteralExpr() : Expr(ExprKind::VecLiteral) { type = HackType::Vec; }
};

// dict["a" => 1, "b" => 2]
struct DictLiteralExpr : Expr {
  std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;
  DictLiteralExpr() : Expr(ExprKind::DictLiteral) { type = HackType::Dict; }
};

// $v[0]
struct SubscriptExpr : Expr {
  std::unique_ptr<Expr> object;
  std::unique_ptr<Expr> index;
  SubscriptExpr(std::unique_ptr<Expr> obj, std::unique_ptr<Expr> idx)
      : Expr(ExprKind::Subscript), object(std::move(obj)),
        index(std::move(idx)) {}
};

// new ClassName(args)
struct NewExpr : Expr {
  std::string className;
  std::vector<std::unique_ptr<Expr>> args;
  explicit NewExpr(std::string name)
      : Expr(ExprKind::New), className(std::move(name)) {
    type = HackType::Object;
  }
};

// $obj->field
struct MemberAccessExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string member;
  MemberAccessExpr(std::unique_ptr<Expr> obj, std::string m)
      : Expr(ExprKind::MemberAccess), object(std::move(obj)),
        member(std::move(m)) {}
};

// $obj->method(args)
struct MethodCallExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string method;
  std::vector<std::unique_ptr<Expr>> args;
  MethodCallExpr(std::unique_ptr<Expr> obj, std::string m)
      : Expr(ExprKind::MethodCall), object(std::move(obj)),
        method(std::move(m)) {}
};

// Function parameter
struct Param {
  std::string name;
  HackType type;
  std::string typeName; // original type name (e.g. "Shape" for interface types)
};

enum class StmtKind {
  Return, VarDecl, Echo, If, While, For, ExprStmt,
  SubscriptAssign, Foreach, MemberAssign
};

struct Stmt {
  StmtKind kind;
  explicit Stmt(StmtKind k) : kind(k) {}
  virtual ~Stmt() = default;
};

struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value; // nullptr means bare "return;"
  explicit ReturnStmt(std::unique_ptr<Expr> v)
      : Stmt(StmtKind::Return), value(std::move(v)) {}
};

struct VarDeclStmt : Stmt {
  std::string name;
  std::unique_ptr<Expr> init;
  HackType varType = HackType::Int;
  VarDeclStmt(std::string n, std::unique_ptr<Expr> e)
      : Stmt(StmtKind::VarDecl), name(std::move(n)), init(std::move(e)) {}
};

struct EchoStmt : Stmt {
  std::unique_ptr<Expr> value;
  explicit EchoStmt(std::unique_ptr<Expr> v)
      : Stmt(StmtKind::Echo), value(std::move(v)) {}
};

struct IfStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::vector<std::unique_ptr<Stmt>> thenBody;
  std::vector<std::unique_ptr<Stmt>> elseBody; // empty if no else
  IfStmt() : Stmt(StmtKind::If) {}
};

struct WhileStmt : Stmt {
  std::unique_ptr<Expr> condition;
  std::vector<std::unique_ptr<Stmt>> body;
  WhileStmt() : Stmt(StmtKind::While) {}
};

struct ForStmt : Stmt {
  std::unique_ptr<Stmt> init;       // VarDeclStmt
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> update;     // VarDeclStmt (reassignment)
  std::vector<std::unique_ptr<Stmt>> body;
  ForStmt() : Stmt(StmtKind::For) {}
};

struct ExprStmtNode : Stmt {
  std::unique_ptr<Expr> expr;
  explicit ExprStmtNode(std::unique_ptr<Expr> e)
      : Stmt(StmtKind::ExprStmt), expr(std::move(e)) {}
};

// $v[0] = 42;
struct SubscriptAssignStmt : Stmt {
  std::unique_ptr<Expr> object;
  std::unique_ptr<Expr> index;
  std::unique_ptr<Expr> value;
  SubscriptAssignStmt(std::unique_ptr<Expr> obj, std::unique_ptr<Expr> idx,
                      std::unique_ptr<Expr> val)
      : Stmt(StmtKind::SubscriptAssign), object(std::move(obj)),
        index(std::move(idx)), value(std::move(val)) {}
};

// foreach ($v as $val) { ... }
// foreach ($d as $key => $val) { ... }
struct ForeachStmt : Stmt {
  std::unique_ptr<Expr> iterable;
  std::string keyVar;   // empty if no key
  std::string valueVar;
  std::vector<std::unique_ptr<Stmt>> body;
  ForeachStmt() : Stmt(StmtKind::Foreach) {}
};

// $obj->field = val;
struct MemberAssignStmt : Stmt {
  std::unique_ptr<Expr> object;
  std::string member;
  std::unique_ptr<Expr> value;
  MemberAssignStmt(std::unique_ptr<Expr> obj, std::string m,
                   std::unique_ptr<Expr> val)
      : Stmt(StmtKind::MemberAssign), object(std::move(obj)),
        member(std::move(m)), value(std::move(val)) {}
};

// Class field
struct FieldDecl {
  std::string name;
  HackType type;
  bool isPublic;
};

struct FuncDecl {
  std::string name;
  bool isAsync = false;
  bool isEntryPoint = false;
  bool isExtern = false;
  std::string returnType;
  HackType hackReturnType = HackType::Void;
  std::vector<Param> params;
  std::vector<std::unique_ptr<Stmt>> body;
};

struct ClassDecl {
  std::string name;
  std::vector<FieldDecl> fields;
  std::vector<FuncDecl> methods;
  std::string implementsInterface; // empty if not implementing
};

// Interface declaration
struct InterfaceMethodSig {
  std::string name;
  HackType returnType;
  std::vector<Param> params;
};

struct InterfaceDecl {
  std::string name;
  std::vector<InterfaceMethodSig> methods;
};

struct ImplBlock {
  std::vector<FuncDecl> functions;
};

struct Program {
  std::vector<FuncDecl> functions;
  std::vector<ClassDecl> classes;
  std::vector<InterfaceDecl> interfaces;
  std::vector<ImplBlock> implBlocks;
};

void dumpAST(const Program &prog, std::ostream &os, int indent = 0);

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens) : tokens_(tokens) {}
  Program parse();

private:
  const Token &peek() const { return tokens_[pos_]; }
  const Token &peekAt(size_t offset) const { return tokens_[pos_ + offset]; }
  const Token &advance() { return tokens_[pos_++]; }
  bool check(TokenKind kind) const { return peek().kind == kind; }
  bool match(TokenKind kind);
  void expect(TokenKind kind, const char *msg);

  std::unique_ptr<Expr> parseExpr();
  std::unique_ptr<Expr> parseOrExpr();
  std::unique_ptr<Expr> parseAndExpr();
  std::unique_ptr<Expr> parseCompareExpr();
  std::unique_ptr<Expr> parseAddExpr();
  std::unique_ptr<Expr> parseConcatExpr();
  std::unique_ptr<Expr> parseMulExpr();
  std::unique_ptr<Expr> parseUnaryExpr();
  std::unique_ptr<Expr> parsePrimaryExpr();
  std::unique_ptr<Expr> parsePostfixExpr();
  std::unique_ptr<Stmt> parseStmt();
  std::vector<std::unique_ptr<Stmt>> parseBlock();
  FuncDecl parseFuncDecl(bool hasEntryPoint);
  ClassDecl parseClassDecl();
  InterfaceDecl parseInterfaceDecl();
  ImplBlock parseImplBlock();

  const std::vector<Token> &tokens_;
  size_t pos_ = 0;
};
