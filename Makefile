CC      = cc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -g
LDFLAGS =

# SDL2 flags (for main.c only)
SDL_CFLAGS  = $(shell pkg-config --cflags sdl2)
SDL_LDFLAGS = $(shell pkg-config --libs sdl2)

SRCS    = main.c cpu6809.c memory.c sam.c vdg.c pia.c dragon.c cassette.c savestate.c
OBJS    = $(SRCS:.c=.o)
TARGET  = idris6809

# All object files except main (for linking into tests)
LIBOBJS = cpu6809.o memory.o sam.o vdg.o pia.o dragon.o cassette.o savestate.o

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

test_dragon: test_dragon.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test_frontend: test_frontend.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test_cassette: test_cassette.o cassette.o pia.o
	$(CC) $(LDFLAGS) -o $@ $^

test_cpu6809: test_cpu6809.o cpu6809.o memory.o
	$(CC) $(LDFLAGS) -o $@ $^

test_graphics: test_graphics.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test_savestate: test_savestate.o $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

test: test_cpu6809 test_memory test_sam test_vdg test_pia test_dragon test_frontend test_cassette test_graphics test_savestate
	@total_pass=0; total_fail=0; \
	for t in ./test_cpu6809 ./test_memory ./test_sam ./test_vdg ./test_pia \
	         ./test_dragon ./test_frontend ./test_cassette ./test_graphics ./test_savestate; do \
		output=$$($$t 2>&1); \
		echo "$$output" | grep -v "^$$" | grep -v "^===" | grep -v "^  " | grep -v "^[A-Z]" > /dev/null; \
		line=$$(echo "$$output" | grep "passed,"); \
		echo "$$line"; \
		p=$$(echo "$$line" | sed 's/.*: \([0-9]*\) passed.*/\1/'); \
		f=$$(echo "$$line" | sed 's/.*passed, \([0-9]*\) failed.*/\1/'); \
		total_pass=$$((total_pass + p)); \
		total_fail=$$((total_fail + f)); \
	done; \
	echo ""; \
	echo "=== Total: $$total_pass passed, $$total_fail failed ==="; \
	[ $$total_fail -eq 0 ]

clean:
	rm -f $(OBJS) $(TARGET) \
		test_memory test_memory.o \
		test_sam test_sam.o \
		test_vdg test_vdg.o \
		test_pia test_pia.o \
		test_dragon test_dragon.o \
		test_frontend test_frontend.o \
		test_cassette test_cassette.o \
		test_cpu6809 test_cpu6809.o \
		test_graphics test_graphics.o \
		test_savestate test_savestate.o

run: $(TARGET)
	./$(TARGET)

# Dependencies
main.o: main.c dragon.h cpu6809.h memory.h sam.h vdg.h pia.h cassette.h savestate.h
dragon.o: dragon.c dragon.h cpu6809.h memory.h sam.h vdg.h pia.h cassette.h
cassette.o: cassette.c cassette.h
savestate.o: savestate.c savestate.h dragon.h memory.h
cpu6809.o: cpu6809.c cpu6809.h memory.h
memory.o: memory.c memory.h
sam.o: sam.c sam.h memory.h
vdg.o: vdg.c vdg.h
pia.o: pia.c pia.h
test_cpu6809.o: test_cpu6809.c cpu6809.h memory.h
test_memory.o: test_memory.c memory.h
test_sam.o: test_sam.c sam.h memory.h
test_vdg.o: test_vdg.c vdg.h
test_pia.o: test_pia.c pia.h
test_dragon.o: test_dragon.c dragon.h
test_frontend.o: test_frontend.c dragon.h
test_cassette.o: test_cassette.c cassette.h pia.h
test_graphics.o: test_graphics.c dragon.h vdg.h
test_savestate.o: test_savestate.c savestate.h dragon.h

.PHONY: all clean run test
