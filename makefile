CC=clang
AR=llvm-ar
CFLAGS=-Werror -std=c23 -g

BINS=9test debug disasm

all: test $(BINS)

.PHONY: all clean test

#
# libgb.a
#

LIB_GB=src/gb/libgb.a
SRCS_GB=src/gb/cpu.c src/gb/gameboy.c
TESTS_GB=src/gb/cpu_test.c

DEPS_GB=$(SRCS_GB:.c=.d) $(TESTS_GB:.c=.d)
-include $(DEPS_GB)

$(LIB_GB): $(SRCS_GB:.c=.o)

src/gb/%_test: src/gb/%_test.o $(LIB_GB)
	$(CC) $(CFLAGS) $^ -o $@


#
# lib9.a
#

LIB_9=src/9/lib9.a
SRCS_9=src/9/9p.c src/9/9fsys.c src/9/acme.c src/9/thrd.c
TESTS_9=src/9/9p_test.c src/9/9fsys_test.c

DEPS_9=$(SRCS_9:.c=.d) $(TESTS_9:.c=.d)
-include $(DEPS_9)

$(LIB_9): $(SRCS_9:.c=.o)

src/9/%_test: src/9/%_test.o $(LIB_9)
	$(CC) $(CFLAGS) $^ -o $@


#
# binaries
#

9test: src/9test.c $(LIB_9)
	$(CC) $(CFLAGS) $^ -o $@

debug: src/debug.c $(LIB_GB)
	$(CC) $(CFLAGS) $^ -o $@

disasm: src/disasm.c $(LIB_GB)
	$(CC) $(CFLAGS) $^ -o $@


#
# testing
#

TESTS=$(TESTS_GB:.c=) $(TESTS_9:.c=)

test: $(TESTS)
	@for test in $^; do echo $$test ; ./$$test; done


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.d: %.c
	$(CC) $(CFLAGS) -MMD -MF $@ -c $< -o $(@:.d=.o)
%.a:
	$(AR) rcs $@ $^

clean:
	rm -f $(SRCS_GB:.c=.o) $(TESTS_GB:.c=.o) $(DEPS_GB) $(LIB_GB)\
		$(SRCS_9:.c=.o) $(TESTS_9:.c=.o) $(DEPS_9) $(LIB_9)\
		$(TESTS) $(BINS)