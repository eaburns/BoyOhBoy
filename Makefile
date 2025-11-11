CC=clang
CFLAGS=-std=c23 -g
LDFLAGS=-std=c23 -g

# Objects shared among binaries.
OBJS=gameboy.o cpu.o

# Different buildable binaries
BINS=debug disasm

# Test binaries.
TESTS=cpu_test

all: $(OBJS) $(BINS) $(TESTS) run_tests

debug: $(OBJS) debug.c
	$(CC) $(LDFLAGS) $^ -o debug

disasm: $(OBJS) disasm.c
	$(CC) $(LDFLAGS) $^ -o disasm

cpu_test: $(OBJS) cpu_test.c
	$(CC) $(LDFLAGS) $^ -o cpu_test

run_tests: $(TESTS)
	@for test in $^; do echo $$test ; ./$$test; done

clean:
	rm -f *.o $(BINS) $(TESTS)

# Everything depends on gameboy.h.
%.o: %.c gameboy.h
	$(CC) $(CFLAGS) -c $< -o $@