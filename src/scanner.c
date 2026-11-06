#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

// How deeply interpolations can nest inside each other.
#define MAX_INTERPOLATION_NESTING 8

typedef struct {
  const char* start;
  const char* current;
  int line;
  // For each interpolation we are inside, how many "{" are still open. When
  // the count for the innermost one reaches zero, the next "}" ends it and
  // the string continues.
  int braces[MAX_INTERPOLATION_NESTING];
  int interpolationDepth;
} Scanner;

Scanner scanner;
void initScanner(const char* source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
  scanner.interpolationDepth = 0;
}
static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}
static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}
static bool isAtEnd() {
  return *scanner.current == '\0';
}
static char advance() {
  scanner.current++;
  return scanner.current[-1];
}
static char peek() {
  return *scanner.current;
}
static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}
static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;
  scanner.current++;
  return true;
}
static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}
static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}
static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        scanner.line++;
        advance();
        break;
      case '/':
        if (peekNext() == '/') {
          // A comment goes until the end of the line.
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}
static TokenType checkKeyword(int start, int length,
    const char* rest, TokenType type) {
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }

  return TOKEN_IDENTIFIER;
}
static TokenType identifierType() {
  switch (scanner.start[0]) {
    case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
    case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
          case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
        }
      }
      break;
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
          case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
        }
      }
      break;
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
  }

  return TOKEN_IDENTIFIER;
}
static Token identifier() {
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return makeToken(identifierType());
}
static Token number() {
  while (isDigit(peek())) advance();

  // Look for a fractional part.
  if (peek() == '.' && isDigit(peekNext())) {
    // Consume the ".".
    advance();

    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}
// Scans the rest of a string, from just after the opening quote or just after
// the "}" that ends an interpolation. Stops at the closing quote (a
// TOKEN_STRING) or at the next "${" (a TOKEN_INTERPOLATION).
static Token string() {
  const char* contents = scanner.current;
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '$' && peekNext() == '{') {
      if (scanner.interpolationDepth == MAX_INTERPOLATION_NESTING) {
        return errorToken("Interpolation nested too deeply.");
      }

      Token token = makeToken(TOKEN_INTERPOLATION);
      token.start = contents;
      token.length = (int)(scanner.current - contents);

      // Consume the "${".
      advance();
      advance();
      scanner.braces[scanner.interpolationDepth++] = 1;
      return token;
    }

    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  Token token = makeToken(TOKEN_STRING);
  token.start = contents;
  token.length = (int)(scanner.current - contents);

  // The closing quote.
  advance();
  return token;
}
Token scanToken() {
  skipWhitespace();
  scanner.start = scanner.current;

  if (isAtEnd()) {
    if (scanner.interpolationDepth > 0) {
      scanner.interpolationDepth = 0;
      return errorToken("Unterminated string interpolation.");
    }
    return makeToken(TOKEN_EOF);
  }

  char c = advance();
  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{':
      if (scanner.interpolationDepth > 0) {
        scanner.braces[scanner.interpolationDepth - 1]++;
      }
      return makeToken(TOKEN_LEFT_BRACE);
    case '}':
      if (scanner.interpolationDepth > 0 &&
          --scanner.braces[scanner.interpolationDepth - 1] == 0) {
        // The end of an interpolated expression. Keep scanning the string.
        scanner.interpolationDepth--;
        return string();
      }
      return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
    case '?': return makeToken(TOKEN_QUESTION);
    case ':': return makeToken(TOKEN_COLON);
    case '!':
      return makeToken(
          match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(
          match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(
          match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(
          match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return string();
  }

  return errorToken("Unexpected character.");
}
