// 16.1: scans string literals with interpolation and prints the tokens.
#include <stdio.h>

#include "../src/scanner.h"

static const char* names[] = {
  "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACE", "RIGHT_BRACE", "COMMA", "DOT",
  "MINUS", "PLUS", "SEMICOLON", "SLASH", "STAR", "QUESTION", "COLON", "BANG",
  "BANG_EQUAL", "EQUAL", "EQUAL_EQUAL", "GREATER", "GREATER_EQUAL", "LESS",
  "LESS_EQUAL", "IDENTIFIER", "STRING", "NUMBER", "INTERPOLATION", "AND",
  "CLASS", "ELSE", "FALSE", "FOR", "FUN", "IF", "NIL", "OR", "PRINT",
  "RETURN", "SUPER", "THIS", "TRUE", "VAR", "WHILE", "ERROR", "EOF",
};

static void scan(const char* source) {
  printf("%s\n", source);
  initScanner(source);
  for (;;) {
    Token token = scanToken();
    printf("  TOKEN_%-14s '%.*s'\n", names[token.type], token.length,
           token.start);
    if (token.type == TOKEN_EOF) break;
  }
  printf("\n");
}

int main(void) {
  // The two examples from the challenge.
  scan("print \"${drink} will be ready in ${steep + cool} minutes.\";");
  scan("\"Nested ${\"interpolation?! Are you ${\"mad?!\"}\"}\"");

  // A plain string is still one TOKEN_STRING.
  scan("\"no interpolation\"");
  // A lone "$" or "{" in a string is just text.
  scan("\"costs $5 {or more}\"");
  // Braces inside an interpolation don't end it early.
  scan("\"a ${ {b} } c\"");
  // Two interpolations in a row, and one at the very start and end.
  scan("\"${a}${b}\"");
  // Errors.
  scan("\"never closed ${x");
  scan("\"never closed ${x} either");
  return 0;
}
