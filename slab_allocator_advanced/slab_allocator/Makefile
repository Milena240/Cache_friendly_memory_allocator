CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -I./headers
LDFLAGS = -pthread -lgtest -lgtest_main -lbenchmark

SRC_DIR = sources
OBJ_DIR = build
TEST_DIR = tests

# Targets
LIB_OBJ = $(OBJ_DIR)/slab_allocator.o
TEST_BIN = test_runner
BENCH_BIN = bench_runner

all: $(TEST_BIN) $(BENCH_BIN)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Compile Library
$(LIB_OBJ): $(SRC_DIR)/slac.allocator.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile Unit Tests
$(TEST_BIN): $(LIB_OBJ) $(TEST_DIR)/test_slab.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Compile Benchmarks
$(BENCH_BIN): $(LIB_OBJ) $(TEST_DIR)/bench_slab.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

clean:
	rm -rf $(OBJ_DIR) $(TEST_BIN) $(BENCH_BIN)

.PHONY: all test bench clean
