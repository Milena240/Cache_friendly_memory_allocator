CC      = gcc
CFLAGS  = -O0 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET  = bench_clustering
SRCS    = bench_clustering.c node_pool.c

.PHONY: all clean run perf

all: $(TARGET)

$(TARGET): $(SRCS) node_pool.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# Run normally
run: $(TARGET)
	./$(TARGET)

# Run with perf (Linux only) — shows cache-miss hardware counters
perf: $(TARGET)
	perf stat -e cache-misses,cache-references,L1-dcache-load-misses,LLC-load-misses \
	    ./$(TARGET)

clean:
	rm -f $(TARGET)
