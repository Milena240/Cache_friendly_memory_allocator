CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_hot_cold_slab
SRCS   = bench_hot_cold_slab.c hot_cold_slab.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) hot_cold_slab.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
