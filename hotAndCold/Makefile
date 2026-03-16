CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_hotcold
SRCS   = bench_hotcold.c hotcold.c

.PHONY: all clean run perf

all: $(TARGET)

$(TARGET): $(SRCS) hotcold.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

perf: $(TARGET)
	perf stat -e cache-misses,cache-references,L1-dcache-load-misses \
	    ./$(TARGET)

clean:
	rm -f $(TARGET)
