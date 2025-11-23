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
TESTS=cpu_test 9/9p_test 9/9fsys_test

all: $(OBJS) $(BINS) $(TESTS) run_tests

debug: $(OBJS) debug.c
	$(CC) $(LDFLAGS) $^ -o debug

disasm: $(OBJS) disasm.c
	$(CC) $(LDFLAGS) $^ -o disasm

9p_test: 9p_test.c 9/lib9.a
	$(CC) $(LDFLAGS) $^ -o 9p_test

cpu_test: $(OBJS) cpu_test.c
	$(CC) $(LDFLAGS) $^ -o cpu_test

run_tests: $(TESTS)
	@for test in $^; do echo $$test ; ./$$test; done



9/lib9.a: 9/socket.o 9/thrd.o 9/9p.o 9/9fsys.o 9/acme.o
	$(AR) rcs $@ $^
9/9p_test: 9/9p_test.o 9/lib9.a
	$(CC) $(LDFLAGS) $^ -o 9/9p_test
9/9fsys_test: 9/9fsys_test.o 9/lib9.a
	$(CC) $(LDFLAGS) $^ -o 9/9fsys_test

9/9p.o: 9/9p.h 9/thrd.h
9/9fsys.o: 9/9fsys.h 9/9p.h 9/thrd.h
9/acme.o: 9/acme.h 9/9fsys.h 9/thrd.h
9/9p_test.o: 9/9p.h 9/thrd.h
9/9fsys_test.o: 9/9fsys.h

# These ones use fdopen, which is not -std=c23.
9/socket.o: 9/socket.c
	$(CC) $(CFLAGS_POSIX) -c $< -o $@
9/9p_test.o: 9/9p_test.c 9/9p.o
	$(CC) $(CFLAGS_POSIX) -c $< -o $@
9/9fsys_test.o: 9/9fsys_test.c 9/9fsys.o
	$(CC) $(CFLAGS_POSIX) -c $< -o $@



clean:
	rm -f *.o *.a 9/*.o 9/*.a $(BINS) $(TESTS)

# Everything depends on gameboy.h.
%.o: %.c gameboy.h
	$(CC) $(CFLAGS) -c $< -o $@