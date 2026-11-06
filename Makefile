CC ?= cc
CFLAGS ?= -std=c99 -Wall -O2

build/clox: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) src/*.c -o $@

# The reduced instruction set from 18.1.
minimal: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) -DMINIMAL_INSTRUCTIONS src/*.c -o build/clox-minimal

# Strings that point at the source (19.2).
constant: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) -DCONSTANT_STRINGS src/*.c -o build/clox-constant

# The book's hash table of globals, with reused constants (21.1).
table: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) -DGLOBAL_TABLE src/*.c -o build/clox-table

# Prints the bytecode, then the stack and each instruction as it runs.
trace: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) -DDEBUG_PRINT_CODE -DDEBUG_TRACE_EXECUTION src/*.c -o build/clox-trace

# Prints each call to the parsing functions (17.1).
parse-trace: src/*.c src/*.h
	mkdir -p build
	$(CC) $(CFLAGS) -DDEBUG_TRACE_PARSE src/*.c -o build/clox-parse-trace

test:
	./run_tests.sh

# The benchmarks for 20.3, 21.1 and 21.2.
bench:
	./bench/run.sh

clean:
	rm -rf build

.PHONY: minimal constant table trace parse-trace test bench clean
