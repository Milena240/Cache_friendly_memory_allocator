CC      = gcc
CFLAGS  = -O0 -Wall -Wextra -std=c11 -I../struct_splitting
LDFLAGS = -lm

TARGET = bench_adaptive
SRCS   = bench_adaptive.c adaptive.c ../struct_splitting/hotcold.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) adaptive.h ../struct_splitting/hotcold.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
