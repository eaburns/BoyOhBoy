CC=clang
AR=ar
INCLUDE=-I src
WARN=-Werror -Wall -Wno-logical-op-parentheses -Wno-bitwise-op-parentheses
CFLAGS_POSIX=$(WARN) $(INCLUDE) -O2 -g -fsanitize=address
CFLAGS=$(CFLAGS_POSIX) -std=c23

BINS=debug disasm

all: test $(BINS)

.PHONY: all clean test


#
# buffer.o
#
LIB_BUF=src/buf/buffer.o
SRCS_BUF=src/buf/buffer.c
TESTS_BUF=

DEPS_BUF=$(SRCS_BUF:.c=.d) $(TESTS_BUF:.c=.d)
-include $(DEPS_BUF)

src/buf/%_test: src/buf/%_test.o $(LIB_BUF)
	$(CC) $(CFLAGS) $^ -o $@


#
# libgb.a
#

LIB_GB=src/gb/libgb.a
SRCS_GB=src/gb/cpu.c src/gb/ppu.c src/gb/gameboy.c
TESTS_GB=src/gb/cpu_test.c src/gb/gameboy_test.c src/gb/ppu_test.c

DEPS_GB=$(SRCS_GB:.c=.d) $(TESTS_GB:.c=.d)
-include $(DEPS_GB)

$(LIB_GB): $(LIB_BUF) $(SRCS_GB:.c=.o)

src/gb/%_test: src/gb/%_test.o $(LIB_GB)
	$(CC) $(CFLAGS) $^ -o $@


#
# lib9.a
#

LIB_9=src/9/lib9.a
SRCS_9=src/9/9p.c src/9/9fsys.c src/9/acme.c src/9/thread.c src/9/errstr.c src/9/io.c
TESTS_9=src/9/9p_test.c src/9/9fsys_test.c

DEPS_9=$(SRCS_9:.c=.d) $(TESTS_9:.c=.d)
-include $(DEPS_9)

$(LIB_9): $(SRCS_9:.c=.o)

src/9/%_test: src/9/%_test.o $(LIB_9)
	$(CC) $(CFLAGS) $^ -o $@


#
# binaries
#

debug: src/debug.c src/time_ns.o $(LIB_BUF) $(LIB_GB) $(LIB_9)
	$(CC) $(CFLAGS) -lSDL3 $^ -o $@

src/time_ns.o: src/time_ns.c src/time_ns.h
	$(CC) $(CFLAGS_POSIX) -c $< -o $@

disasm: src/disasm.c $(LIB_GB)
	$(CC) $(CFLAGS) $^ -o $@


#
# testing
#

TESTS=$(TESTS_GB:.c=) $(TESTS_9:.c=)

test: $(TESTS)
	@for test in $^; do echo $$test ; ./$$test || exit 1; done


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.d: %.c
	$(CC) $(CFLAGS) -MMD -MF $@ -c $< -o $(@:.d=.o)
%.a:
	$(AR) rcs $@ $^

clean:
	rm -f $(SRCS_GB:.c=.o) $(TESTS_GB:.c=.o) $(DEPS_GB) $(LIB_GB)\
		$(SRCS_9:.c=.o) $(TESTS_9:.c=.o) $(DEPS_9) $(LIB_9)\
		$(SRCS_BUF:.c=.o) $(TESTS_BUF:.c=.o) $(DEPS_BUF) $(LIB_BUF)\
		$(TESTS) $(BINS)