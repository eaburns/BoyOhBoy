CC=clang
AR=llvm-ar
CFLAGS=-std=c23 -g
CFLAGS_POSIX=-g
LDFLAGS=-std=c23 -g

# Objects shared among binaries.
OBJS=gameboy.o cpu.o

# Different buildable binaries
BINS=debug disasm 9p_test

# Test binaries.
TESTS=cpu_test 9p/test

all: $(OBJS) $(BINS) $(TESTS) run_tests

debug: $(OBJS) debug.c
	$(CC) $(LDFLAGS) $^ -o debug

disasm: $(OBJS) disasm.c
	$(CC) $(LDFLAGS) $^ -o disasm

9p_test: 9p_test.c 9p/lib9.a
	$(CC) $(LDFLAGS) $^ -o 9p_test

cpu_test: $(OBJS) cpu_test.c
	$(CC) $(LDFLAGS) $^ -o cpu_test

run_tests: $(TESTS)
	@for test in $^; do echo $$test ; ./$$test; done



9p/test: 9p/test.o 9p/lib9.a
	$(CC) $(LDFLAGS) $^ -o 9p/test

9p/lib9.a: 9p/socket.o 9p/9p.o 9p/9fsys.o
	$(AR) rcs $@ $^

9p/9p.o: 9p/9p.c 9p/9p.h

# These ones use fdopen, which is not -std=c23.
9p/socket.o: 9p/socket.c
	$(CC) $(CFLAGS_POSIX) -c $< -o $@
9p/test.o: 9p/test.c
	$(CC) $(CFLAGS_POSIX) -c $< -o $@



clean:
	rm -f *.o *.a 9p/*.o 9p/*.a $(BINS) $(TESTS)

# Everything depends on gameboy.h.
%.o: %.c gameboy.h
	$(CC) $(CFLAGS) -c $< -o $@