CC      = cc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude
TARGET  = build/nopaldb

SRCS    = src/main.c \
          src/repl/input.c \
          src/repl/meta.c \
          src/repl/repl.c \
          src/compiler/parser.c \
          src/backend/vm.c

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -rf build

.PHONY: all clean
