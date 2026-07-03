CC = gcc
CFLAGS = -Iinclude -Ithird_party/libfvad/include -Wall -Wextra -O2
LDFLAGS = -lm

SRC = $(wildcard src/*.c)
VENDOR_SRC = $(wildcard third_party/libfvad/src/*.c) \
             $(wildcard third_party/libfvad/src/signal_processing/*.c) \
             $(wildcard third_party/libfvad/src/vad/*.c)
EXAMPLES = $(wildcard examples/*.c)
TESTS = $(wildcard tests/*.c)
OBJ_SRC = $(patsubst src/%.c, build/%.o, $(SRC)) \
          $(patsubst third_party/libfvad/src/%.c, build/vendor/%.o, $(VENDOR_SRC))
OBJ_EXAMPLES = $(patsubst examples/%.c, build/%.o, $(EXAMPLES))
OBJ_TESTS = $(patsubst tests/%.c, build/%.o, $(TESTS))
OBJ = $(OBJ_SRC) $(OBJ_EXAMPLES) $(OBJ_TESTS)

all: bin/test

build:
	mkdir -p build

bin:
	mkdir -p bin

lib:
	mkdir -p lib

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/vendor/%.o: third_party/libfvad/src/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: examples/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: tests/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

bin/test: $(OBJ_EXAMPLES) lib/libhannah_audio.a | bin
	$(CC) $(CFLAGS) $(OBJ_EXAMPLES) -Llib -lhannah_audio -o bin/test $(LDFLAGS)

bin/run_tests: $(OBJ_TESTS) lib/libhannah_audio.a | bin
	$(CC) $(CFLAGS) $(OBJ_TESTS) -Llib -lhannah_audio -o bin/run_tests $(LDFLAGS)

lib/libhannah_audio.a: $(OBJ_SRC) | lib
	ar rcs lib/libhannah_audio.a $(OBJ_SRC)

test: bin/run_tests
	./bin/run_tests

clean:
	rm -rf build bin/*
	rm -rf lib/libhannah_audio.a

.PHONY: all clean test