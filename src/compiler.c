#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_CONDITIONAL, // ?:
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;
#if defined(GLOBAL_TABLE) && !defined(NO_CONSTANT_DEDUP)
// 21.1: maps each identifier already in this chunk's constant table to its
// index.
static Table stringConstants;
#endif

// 17.1: with DEBUG_TRACE_PARSE, expression(), parsePrecedence() and the
// prefix and infix parse functions print a line when they are called,
// indented by how deeply they are nested.
#ifdef DEBUG_TRACE_PARSE
static int traceDepth = 0;

static const char* precedenceNames[] = {
  "PREC_NONE", "PREC_ASSIGNMENT", "PREC_CONDITIONAL", "PREC_OR", "PREC_AND",
  "PREC_EQUALITY", "PREC_COMPARISON", "PREC_TERM", "PREC_FACTOR",
  "PREC_UNARY", "PREC_CALL", "PREC_PRIMARY",
};

static void traceCall(const char* format, ...) {
  for (int i = 0; i < traceDepth; i++) printf("| ");
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
  traceDepth++;
}

#define TRACE_CALL(...) traceCall(__VA_ARGS__)
#define TRACE_RETURN() (traceDepth--)
#else
#define TRACE_CALL(...) ((void)0)
#define TRACE_RETURN() ((void)0)
#endif

static Chunk* currentChunk() {
  return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}
static void error(const char* message) {
  errorAt(&parser.previous, message);
}
static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}
static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}
static bool check(TokenType type) {
  return parser.current.type == type;
}
static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}
static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}
static void emitReturn() {
  emitByte(OP_RETURN);
}
static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}
static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}
static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}
static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself.
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}
#ifdef MINIMAL_INSTRUCTIONS
// Loads a constant the compiler made up (nil, true, false or -1), reusing
// the chunk's existing copy if it has one so they don't fill up the table.
static void emitSharedConstant(Value value) {
  ValueArray* constants = &currentChunk()->constants;
  for (int i = 0; i < constants->count && i <= UINT8_MAX; i++) {
    if (valuesEqual(constants->values[i], value)) {
      emitBytes(OP_CONSTANT, (uint8_t)i);
      return;
    }
  }

  emitConstant(value);
}
// Replaces OP_NEGATE. x * -1 is exactly -x, even for 0, where 0 - x would
// give 0 instead of -0.
static void emitNegate() {
  emitSharedConstant(NUMBER_VAL(-1));
  emitByte(OP_MULTIPLY);
}
#endif
// Loads nil. The minimal build has no OP_NIL (18.1).
static void emitNil() {
#ifdef MINIMAL_INSTRUCTIONS
  emitSharedConstant(NIL_VAL);
#else
  emitByte(OP_NIL);
#endif
}
static void endCompiler() {
  emitReturn();
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

#ifdef MINIMAL_INSTRUCTIONS
// 18.1: everything is built from OP_EQUAL, OP_LESS, OP_ADD, OP_MULTIPLY,
// OP_DIVIDE and OP_NOT. a > b is compiled as -a < -b, so the left operand,
// which is already on the stack, gets negated before the right one is
// compiled. That keeps the operands evaluated left to right.
static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  bool flip = operatorType == TOKEN_GREATER ||
              operatorType == TOKEN_LESS_EQUAL;
  if (flip) emitNegate();
  parsePrecedence((Precedence)(rule->precedence + 1));
  if (flip) emitNegate();

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_LESS); break;          // -a < -b
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break; // !(a < b)
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_LESS, OP_NOT); break; // !(-a < -b)
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitNegate(); emitByte(OP_ADD); break; // a + -b
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default: return; // Unreachable.
  }
}
#else
static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitByte(OP_NOT_EQUAL); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitByte(OP_LESS_EQUAL); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default: return; // Unreachable.
  }
}
#endif
// 17.3: cond ? a : b. The condition is already compiled. Like C, the middle
// operand can be any expression, since "?" and ":" surround it, and the
// last one is parsed at PREC_CONDITIONAL so the operator is right
// associative: a ? b : c ? d : e means a ? b : (c ? d : e). Only one of the
// two branches runs.
static void conditional(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  expression();
  consume(TOKEN_COLON,
          "Expect ':' after then branch of conditional operator.");

  int endJump = emitJump(OP_JUMP);
  patchJump(elseJump);
  emitByte(OP_POP);
  parsePrecedence(PREC_CONDITIONAL);
  patchJump(endJump);
}
static void literal(bool canAssign) {
  switch (parser.previous.type) {
#ifdef MINIMAL_INSTRUCTIONS
    case TOKEN_FALSE: emitSharedConstant(BOOL_VAL(false)); break;
    case TOKEN_NIL: emitSharedConstant(NIL_VAL); break;
    case TOKEN_TRUE: emitSharedConstant(BOOL_VAL(true)); break;
#else
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
#endif
    default: return; // Unreachable.
  }
}
static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}
static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}
// The scanner's lexeme for a string is already just its contents (16.1).
static void string(bool canAssign) {
  emitConstant(OBJ_VAL(literalString(parser.previous.start,
                                     parser.previous.length)));
}
// Returns what the global instructions use as an operand for this name.
#ifdef GLOBAL_TABLE
static int identifierConstant(Token* name) {
  Value string = OBJ_VAL(literalString(name->start, name->length));
#ifndef NO_CONSTANT_DEDUP
  // 21.1: reuse the slot if this name is already in the constant table.
  Value index;
  if (tableGet(&stringConstants, string, &index)) {
    return (int)AS_NUMBER(index);
  }
#endif

  uint8_t constant = makeConstant(string);
#ifndef NO_CONSTANT_DEDUP
  tableSet(&stringConstants, string, NUMBER_VAL((double)constant));
#endif
  return constant;
}
#else
// 21.2: finds the slot for this name, or gives it a new, undefined one. The
// slots are in the VM so they last from one line of the REPL to the next.
static int identifierConstant(Token* name) {
  Value string = OBJ_VAL(literalString(name->start, name->length));
  Value slot;
  if (tableGet(&vm.globalNames, string, &slot)) {
    return (int)AS_NUMBER(slot);
  }

  if (vm.globalValues.count > UINT16_MAX) {
    error("Too many global variables.");
    return 0;
  }

  int index = vm.globalValues.count;
  writeValueArray(&vm.globalValues, EMPTY_VAL);
  writeValueArray(&vm.globalIdentifiers, string);
  tableSet(&vm.globalNames, string, NUMBER_VAL((double)index));
  return index;
}
#endif
static void emitGlobal(uint8_t instruction, int operand) {
  emitByte(instruction);
#ifndef GLOBAL_TABLE
  emitByte((operand >> 8) & 0xff);
#endif
  emitByte(operand & 0xff);
}
static void namedVariable(Token name, bool canAssign) {
  int arg = identifierConstant(&name);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitGlobal(OP_SET_GLOBAL, arg);
  } else {
    emitGlobal(OP_GET_GLOBAL, arg);
  }
}
static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}
static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // Compile the operand.
  parsePrecedence(PREC_UNARY);

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG: emitByte(OP_NOT); break;
#ifdef MINIMAL_INSTRUCTIONS
    case TOKEN_MINUS: emitNegate(); break;
#else
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
#endif
    default: return; // Unreachable.
  }
}
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, // [big]
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_QUESTION]      = {NULL,     conditional, PREC_CONDITIONAL},
  [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_INTERPOLATION] = {NULL,     NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};
#ifdef DEBUG_TRACE_PARSE
static const char* parseFnName(ParseFn fn) {
  if (fn == binary) return "binary";
  if (fn == conditional) return "conditional";
  if (fn == grouping) return "grouping";
  if (fn == literal) return "literal";
  if (fn == number) return "number";
  if (fn == string) return "string";
  if (fn == unary) return "unary";
  if (fn == variable) return "variable";
  return "?";
}
#endif
static void parsePrecedence(Precedence precedence) {
  TRACE_CALL("parsePrecedence(%s)", precedenceNames[precedence]);
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    TRACE_RETURN();
    return;
  }

  TRACE_CALL("%s() '%.*s'", parseFnName(prefixRule),
             parser.previous.length, parser.previous.start);
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);
  TRACE_RETURN();

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    TRACE_CALL("%s() '%.*s'", parseFnName(infixRule),
               parser.previous.length, parser.previous.start);
    infixRule(canAssign);
    TRACE_RETURN();
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
  TRACE_RETURN();
}
static int parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  return identifierConstant(&parser.previous);
}
static void defineVariable(int global) {
  emitGlobal(OP_DEFINE_GLOBAL, global);
}
static ParseRule* getRule(TokenType type) {
  return &rules[type];
}
static void expression() {
  TRACE_CALL("expression()");
  parsePrecedence(PREC_ASSIGNMENT);
  TRACE_RETURN();
}
static void varDeclaration() {
  int global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitNil();
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}
static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}
static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}
static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:
        ; // Do nothing.
    }

    advance();
  }
}
static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}
static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else {
    expressionStatement();
  }
}

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;
#if defined(GLOBAL_TABLE) && !defined(NO_CONSTANT_DEDUP)
  initTable(&stringConstants);
#endif

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  endCompiler();
#if defined(GLOBAL_TABLE) && !defined(NO_CONSTANT_DEDUP)
  freeTable(&stringConstants);
#endif
  return !parser.hadError;
}
