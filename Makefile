CC=clang
CFLAGS=-std=c23 -g

OBJS=gameboy.o cpu.o

all: $(OBJS) disasm cpu_test run_tests

disasm: $(OBJS) disasm.o
	$(CC) $(LDFLAGS) $^ -o disasm

cpu_test: $(OBJS) cpu_test.o
	$(CC) $(LDFLAGS) $^ -o cpu_test

run_tests: cpu_test
	./cpu_test

clean:
	rm -f $(OBJS) disasm

# Everything depends on gameboy.h.
%.o: %.c gameboy.h
	$(CC) $(CFLAGS) -c $< -o $@