CC=clang
CFLAGS=-std=c23

OBJS=gameboy.o cpu.o disasm.o

all: disasm $(OBJS)

disasm: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o disasm

clean:
	rm -f $(OBJS) disasm

# Everything depends on gameboy.h.
%.o: %.c gameboy.h
	$(CC) $(CFLAGS) -c $< -o $@