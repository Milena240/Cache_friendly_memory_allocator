CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_dynamic_pool
SRCS   = bench_dynamic_pool.c dynamic_pool.c

.PHONY: all clean run perf

all: $(TARGET)

$(TARGET): $(SRCS) dynamic_pool.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

perf: $(TARGET)
	perf stat -e cache-misses,cache-references,L1-dcache-load-misses \
	    ./$(TARGET)

clean:
	rm -f $(TARGET)
