CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_page_slab
SRCS   = bench_page_slab.c page_slab.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) page_slab.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
