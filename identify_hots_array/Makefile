CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_array_profiler
SRCS   = bench_array_profiler.c array_profiler.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) array_profiler.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
