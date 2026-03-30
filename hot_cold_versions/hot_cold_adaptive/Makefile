CC      = gcc
CFLAGS  = -O0 -Wall -Wextra -std=c11
LDFLAGS = -lm

TARGET = bench_adaptive
SRCS   = bench_adaptive.c adaptive.c hotcold.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) adaptive.h hotcold.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
