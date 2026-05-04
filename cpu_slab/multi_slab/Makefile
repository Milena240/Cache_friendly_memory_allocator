CC      = gcc
CFLAGS  = -O0 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_multi_slab
SRCS   = bench_multi_slab.c multi_slab.c ../hot_cold_slab_allocator/hot_cold_slab.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) multi_slab.h ../hot_cold_slab_allocator/hot_cold_slab.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
