# Crafting Interpreters — Chapters 19, 20 & 21 Challenges

Solutions to the end-of-chapter challenges from [_Crafting Interpreters_](https://craftinginterpreters.com/) by Robert Nystrom, chapters **19 (Strings)**, **20 (Hash Tables)** and **21 (Global Variables)**. This is clox, the bytecode virtual machine in C, with the complete code through chapter 21, and it builds on my chapters 16-18 solutions. It runs programs made of `print`, expression and `var` statements.

## Build and run

```sh
make                  # builds build/clox (a REPL, or pass a file)
make constant         # build/clox-constant, strings that point at the source (19.2)
make table            # build/clox-table, the book's table of globals (21.1)
make minimal          # build/clox-minimal, the reduced instruction set (18.1)
make trace            # prints the bytecode and each instruction as it runs
./run_tests.sh        # runs the tests in all four builds
make bench            # the benchmarks for 20.3, 21.1 and 21.2
```

## Chapter 19 — Strings

1. **Flexible array members.** `ObjString` ends in `char chars[]`, so a string
   is one allocation. `makeString()` allocates one and `finishString()` interns
   it. If an equal string already exists, the new one is freed, so a string
   only goes on the object list after it is known to be unique.
2. **Constant strings.** With `-DCONSTANT_STRINGS`, `chars` is a pointer and
   `ownsChars` says who frees it. Literals and identifiers point at the source,
   so the source has to outlive the VM: `keepSource()` holds on to it, and the
   REPL gives each line its own buffer. See `tests/repl.input`.
3. **`+` with a string and another type.** Written answer (submitted separately).

## Chapter 20 — Hash Tables

1. **Other key types.** Keys are `Value`s, with a new internal `VAL_EMPTY` for
   empty buckets. Numbers use a real mixing function, because Lua's hash makes
   whole numbers collide in a table whose size is a power of two. See
   `tests/ch20_table.c`. What classes would add is a written answer.
2. **Hash tables in open source systems.** Written answer (submitted separately).
3. **Benchmarks.** `bench/table_bench.c`. These found that the book's table
   grows without limit when deletes are common, so `tableSet()` now cleans up
   tombstones instead of doubling when most of the table is tombstones
   (`-DBOOK_TABLE_GROWTH` turns that off).

## Chapter 21 — Global Variables

1. **Reusing constants.** `identifierConstant()` keeps a table of the names
   already in the chunk. This is in the `-DGLOBAL_TABLE` build, because 21.2
   doesn't put names in the constant table at all.
2. **Faster globals.** The compiler gives each name a slot, and the
   instructions index an array, with a two byte operand so a program can have
   more than 256 globals. Errors still name the variable. Two to three times
   faster in `bench/globals_bench.c`. `tests/ch21_globals.c` runs the same
   programs in both builds.
3. **Undefined variables in scripts.** Written answer (submitted separately).

## Files changed from my chapter 18 code

| File | Change |
|---|---|
| `object.h`, `object.c` | New in chapter 19. Flexible array strings (19.1); constant strings (19.2) |
| `table.h`, `table.c` | New in chapter 20. `Value` keys and hashes (20.1); growth that counts tombstones (20.3) |
| `value.h`, `value.c` | `VAL_OBJ`; `VAL_EMPTY` (20.1, 21.2) |
| `memory.h`, `memory.c` | `ALLOCATE`, `freeObjects()`, allocation counters |
| `compiler.c` | Statements, variables and `canAssign`; `string()`; reusing constants (21.1); slots (21.2) |
| `chunk.h`, `debug.c`, `vm.h`, `vm.c` | `OP_PRINT` and the global instructions; `concatenate()`; `runChunk()` for benchmarks; `keepSource()` (19.2); slots (21.2) |
| `main.c` | The REPL's line buffers (19.2) |
| `common.h` | Lists the build options |

A string containing `${` still scans as interpolation from 16.1, but the
compiler doesn't use those tokens yet, so it is a compile error.

## Tests

`./run_tests.sh` builds every program in `tests/`, runs it, and compares its
output with its `.expected` file, in four builds: default, minimal, constant
and table. A test uses `<name>.<build>.expected` if it has one. All 36 checks
pass, and they also pass with
`SANITIZE="-fsanitize=address,undefined -g"`.
