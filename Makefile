CC      = cc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -g
LDFLAGS =

# SDL2 flags (for main.c only)
SDL_CFLAGS  = $(shell pkg-config --cflags sdl2)
SDL_LDFLAGS = $(shell pkg-config --libs sdl2)

SRCS    = main.c cpu6809.c memory.c sam.c vdg.c pia.c acia.c dragon.c
OBJS    = $(SRCS:.c=.o)
TARGET  = idris6809

# All object files except main (for linking into tests)
LIBOBJS = cpu6809.o memory.o sam.o vdg.o pia.o acia.o dragon.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(SDL_LDFLAGS) -o $@ $^

# main.c needs SDL headers
main.o: main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Tests (no SDL dependency)
test_memory: test_memory.o memory.o
	$(CC) $(LDFLAGS) -o $@ $^

test_sam: test_sam.o sam.o memory.o
	$(CC) $(LDFLAGS) -o $@ $^

test_vdg: test_vdg.o vdg.o
	$(CC) $(LDFLAGS) -o $@ $^

test_pia: test_pia.o pia.o
	$(CC) $(LDFLAGS) -o $@ $^

test_acia: test_acia.o acia.o
	$(CC) $(LDFLAGS) -o $@ $^

test_dragon: test_dragon.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test_frontend: test_frontend.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test: test_memory test_sam test_vdg test_pia test_acia test_dragon test_frontend
	./test_memory
	./test_sam
	./test_vdg
	./test_pia
	./test_acia
	./test_dragon
	./test_frontend

clean:
	rm -f $(OBJS) $(TARGET) \
		test_memory test_memory.o \
		test_sam test_sam.o \
		test_vdg test_vdg.o \
		test_pia test_pia.o \
		test_acia test_acia.o \
		test_dragon test_dragon.o \
		test_frontend test_frontend.o

run: $(TARGET)
	./$(TARGET)

# Dependencies
main.o: main.c dragon.h cpu6809.h memory.h sam.h vdg.h pia.h acia.h
dragon.o: dragon.c dragon.h cpu6809.h memory.h sam.h vdg.h pia.h acia.h
cpu6809.o: cpu6809.c cpu6809.h memory.h
memory.o: memory.c memory.h
sam.o: sam.c sam.h memory.h
vdg.o: vdg.c vdg.h
pia.o: pia.c pia.h
acia.o: acia.c acia.h
test_memory.o: test_memory.c memory.h
test_sam.o: test_sam.c sam.h memory.h
test_vdg.o: test_vdg.c vdg.h
test_pia.o: test_pia.c pia.h
test_acia.o: test_acia.c acia.h
test_dragon.o: test_dragon.c dragon.h
test_frontend.o: test_frontend.c dragon.h

.PHONY: all clean run test
