CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=gnu11
LDFLAGS = -lm

TARGET = bench_array_tree
SRCS   = bench_array_tree.c array_tree.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) array_tree.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
