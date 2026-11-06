#!/bin/bash
# Builds and runs the benchmarks, with the same compiler flags each time.
cd "$(dirname "$0")/.."
CC=${CC:-cc}
CFLAGS="-std=c99 -O2 -Wall -Wno-unused-parameter"
SOURCES=$(ls src/*.c | grep -v main.c)
mkdir -p build

echo "== 20.3 hash table =="
$CC $CFLAGS $SOURCES bench/table_bench.c -o build/table_bench || exit 1
build/table_bench

echo
echo "== 20.1 number keys with Lua's hash =="
$CC $CFLAGS -DLUA_DOUBLE_HASH $SOURCES bench/table_bench.c -o build/table_bench_lua || exit 1
build/table_bench_lua numbers

echo
echo "== 20.3 churn with the book's rule for growing =="
$CC $CFLAGS -DBOOK_TABLE_GROWTH $SOURCES bench/table_bench.c -o build/table_bench_growth || exit 1
build/table_bench_growth churn

echo
echo "== 21.2 indexed slots =="
$CC $CFLAGS $SOURCES bench/globals_bench.c -o build/globals_slots || exit 1
build/globals_slots

echo
echo "== 21.1 hash table, constants reused =="
$CC $CFLAGS -DGLOBAL_TABLE $SOURCES bench/globals_bench.c -o build/globals_dedup || exit 1
build/globals_dedup

echo
echo "== hash table, no constants reused =="
$CC $CFLAGS -DGLOBAL_TABLE -DNO_CONSTANT_DEDUP $SOURCES bench/globals_bench.c -o build/globals_nodedup || exit 1
build/globals_nodedup
