CC      = gcc
CFLAGS  = -O0 -Wall -Wextra -std=c11
LDFLAGS = -lpthread -lm

TARGET = bench_thread_slab
SRCS   = bench_thread_slab.c thread_slab.c \
         ../multi_slab/multi_slab.c \
         ../hot_cold_slab_allocator/hot_cold_slab.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) thread_slab.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
