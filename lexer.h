#pragma once
#include <string>
#include <vector>

enum class TokenKind {
  // Attributes
  AttrStart,    // <<
  AttrEnd,      // >>
  Underscore2,  // __

  // Keywords
  KwAsync,
  KwFunction,
  KwAwaitable,
  KwReturn,
  KwEcho,
  KwTrue,
  KwFalse,
  KwIf,
  KwElse,
  KwWhile,
  KwFor,
  KwForeach,
  KwAs,
  KwClass,
  KwNew,
  KwPublic,
  KwPrivate,
  KwInterface,
  KwImplements,
  KwImpl,

  // Symbols
  LParen,    // (
  RParen,    // )
  LBrace,    // {
  RBrace,    // }
  LBracket,  // [
  RBracket,  // ]
  LAngle,    // <
  RAngle,    // >
  Colon,     // :
  Comma,     // ,
  Semicolon, // ;
  Plus,      // +
  Minus,     // -
  Star,      // *
  Slash,     // /
  Percent,   // %
  Equals,    // =
  Dot,       // .
  Bang,      // !

  // Multi-char operators
  EqEq,      // ==
  BangEq,    // !=
  LessEq,    // <=
  GreaterEq, // >=
  AmpAmp,    // &&
  PipePipe,  // ||
  FatArrow,  // =>
  Arrow,     // ->

  // Literals
  IntLiteral,
  FloatLiteral,
  StringLiteral,

  // Identifiers & types
  Identifier,
  Variable,  // $name

  // Special
  Eof,
  Unknown,
};

struct Token {
  TokenKind kind;
  std::string text;
  int line;
  int col;
};

class Lexer {
public:
  explicit Lexer(const std::string &source) : src_(source) {}
  std::vector<Token> tokenize();

private:
  char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
  char peekNext() const { return pos_ + 1 < src_.size() ? src_[pos_ + 1] : '\0'; }
  char advance() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }
  void skipWhitespaceAndComments();
  Token readIdentifierOrKeyword();

  const std::string &src_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
};
