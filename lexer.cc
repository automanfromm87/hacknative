#include "lexer.h"

void Lexer::skipWhitespaceAndComments() {
  while (pos_ < src_.size()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') {
      advance();
      col_++;
    } else if (c == '\n') {
      advance();
      line_++;
      col_ = 1;
    } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
      // Line comment
      while (pos_ < src_.size() && peek() != '\n')
        advance();
    } else {
      break;
    }
  }
}

Token Lexer::readIdentifierOrKeyword() {
  int startCol = col_;
  std::string text;
  while (pos_ < src_.size() && (isalnum(peek()) || peek() == '_')) {
    text += advance();
    col_++;
  }

  TokenKind kind = TokenKind::Identifier;
  if (text == "async")
    kind = TokenKind::KwAsync;
  else if (text == "function")
    kind = TokenKind::KwFunction;
  else if (text == "Awaitable")
    kind = TokenKind::KwAwaitable;
  else if (text == "return")
    kind = TokenKind::KwReturn;
  else if (text == "echo")
    kind = TokenKind::KwEcho;
  else if (text == "true")
    kind = TokenKind::KwTrue;
  else if (text == "false")
    kind = TokenKind::KwFalse;
  else if (text == "if")
    kind = TokenKind::KwIf;
  else if (text == "else")
    kind = TokenKind::KwElse;
  else if (text == "while")
    kind = TokenKind::KwWhile;
  else if (text == "for")
    kind = TokenKind::KwFor;
  else if (text == "foreach")
    kind = TokenKind::KwForeach;
  else if (text == "as")
    kind = TokenKind::KwAs;
  else if (text == "class")
    kind = TokenKind::KwClass;
  else if (text == "new")
    kind = TokenKind::KwNew;
  else if (text == "public")
    kind = TokenKind::KwPublic;
  else if (text == "private")
    kind = TokenKind::KwPrivate;
  else if (text == "interface")
    kind = TokenKind::KwInterface;
  else if (text == "implements")
    kind = TokenKind::KwImplements;
  else if (text == "impl")
    kind = TokenKind::KwImpl;
  else if (text == "__")
    kind = TokenKind::Underscore2;

  return {kind, text, line_, startCol};
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (pos_ < src_.size()) {
    skipWhitespaceAndComments();
    if (pos_ >= src_.size())
      break;

    char c = peek();
    int startCol = col_;

    // Multi-char operators first
    if (c == '=' && peekNext() == '>') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::FatArrow, "=>", line_, startCol});
    } else if (c == '=' && peekNext() == '=') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::EqEq, "==", line_, startCol});
    } else if (c == '!' && peekNext() == '=') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::BangEq, "!=", line_, startCol});
    } else if (c == '-' && peekNext() == '>') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::Arrow, "->", line_, startCol});
    } else if (c == '<' && peekNext() == '<') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::AttrStart, "<<", line_, startCol});
    } else if (c == '<' && peekNext() == '=') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::LessEq, "<=", line_, startCol});
    } else if (c == '>' && peekNext() == '>') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::AttrEnd, ">>", line_, startCol});
    } else if (c == '>' && peekNext() == '=') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::GreaterEq, ">=", line_, startCol});
    } else if (c == '&' && peekNext() == '&') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::AmpAmp, "&&", line_, startCol});
    } else if (c == '|' && peekNext() == '|') {
      advance(); advance(); col_ += 2;
      tokens.push_back({TokenKind::PipePipe, "||", line_, startCol});
    } else if (c == '<') {
      advance(); col_++;
      tokens.push_back({TokenKind::LAngle, "<", line_, startCol});
    } else if (c == '>') {
      advance(); col_++;
      tokens.push_back({TokenKind::RAngle, ">", line_, startCol});
    } else if (c == '(') {
      advance(); col_++;
      tokens.push_back({TokenKind::LParen, "(", line_, startCol});
    } else if (c == ')') {
      advance(); col_++;
      tokens.push_back({TokenKind::RParen, ")", line_, startCol});
    } else if (c == '{') {
      advance(); col_++;
      tokens.push_back({TokenKind::LBrace, "{", line_, startCol});
    } else if (c == '}') {
      advance(); col_++;
      tokens.push_back({TokenKind::RBrace, "}", line_, startCol});
    } else if (c == '[') {
      advance(); col_++;
      tokens.push_back({TokenKind::LBracket, "[", line_, startCol});
    } else if (c == ']') {
      advance(); col_++;
      tokens.push_back({TokenKind::RBracket, "]", line_, startCol});
    } else if (c == ':') {
      advance(); col_++;
      tokens.push_back({TokenKind::Colon, ":", line_, startCol});
    } else if (c == ',') {
      advance(); col_++;
      tokens.push_back({TokenKind::Comma, ",", line_, startCol});
    } else if (c == '+') {
      advance(); col_++;
      tokens.push_back({TokenKind::Plus, "+", line_, startCol});
    } else if (c == '-') {
      advance(); col_++;
      tokens.push_back({TokenKind::Minus, "-", line_, startCol});
    } else if (c == '*') {
      advance(); col_++;
      tokens.push_back({TokenKind::Star, "*", line_, startCol});
    } else if (c == '/') {
      advance(); col_++;
      tokens.push_back({TokenKind::Slash, "/", line_, startCol});
    } else if (c == '%') {
      advance(); col_++;
      tokens.push_back({TokenKind::Percent, "%", line_, startCol});
    } else if (c == '=') {
      advance(); col_++;
      tokens.push_back({TokenKind::Equals, "=", line_, startCol});
    } else if (c == '!') {
      advance(); col_++;
      tokens.push_back({TokenKind::Bang, "!", line_, startCol});
    } else if (c == '.') {
      advance(); col_++;
      tokens.push_back({TokenKind::Dot, ".", line_, startCol});
    } else if (c == ';') {
      advance(); col_++;
      tokens.push_back({TokenKind::Semicolon, ";", line_, startCol});
    } else if (c == '$') {
      advance(); col_++;
      std::string name;
      while (pos_ < src_.size() && (isalnum(peek()) || peek() == '_')) {
        name += advance();
        col_++;
      }
      tokens.push_back({TokenKind::Variable, name, line_, startCol});
    } else if (c == '"') {
      advance(); col_++;
      std::string str;
      while (pos_ < src_.size() && peek() != '"') {
        char ch = advance();
        col_++;
        if (ch == '\\' && pos_ < src_.size()) {
          char esc = advance();
          col_++;
          switch (esc) {
          case 'n': str += '\n'; break;
          case 't': str += '\t'; break;
          case '\\': str += '\\'; break;
          case '"': str += '"'; break;
          default: str += '\\'; str += esc; break;
          }
        } else {
          str += ch;
        }
      }
      if (pos_ < src_.size()) { advance(); col_++; } // closing "
      tokens.push_back({TokenKind::StringLiteral, str, line_, startCol});
    } else if (isdigit(c)) {
      int numStartCol = col_;
      std::string num;
      while (pos_ < src_.size() && isdigit(peek())) {
        num += advance();
        col_++;
      }
      if (pos_ < src_.size() && peek() == '.' && pos_ + 1 < src_.size() && isdigit(src_[pos_ + 1])) {
        num += advance(); col_++; // '.'
        while (pos_ < src_.size() && isdigit(peek())) {
          num += advance();
          col_++;
        }
        tokens.push_back({TokenKind::FloatLiteral, num, line_, numStartCol});
      } else {
        tokens.push_back({TokenKind::IntLiteral, num, line_, numStartCol});
      }
    } else if (isalpha(c) || c == '_') {
      tokens.push_back(readIdentifierOrKeyword());
    } else {
      advance(); col_++;
      tokens.push_back({TokenKind::Unknown, std::string(1, c), line_, startCol});
    }
  }

  tokens.push_back({TokenKind::Eof, "", line_, col_});
  return tokens;
}
