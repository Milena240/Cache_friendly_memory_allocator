CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iheaders

SOURCES = sources/allocator.cpp
TEST_SRC = tests/basic_test.cpp
TARGET = basic_test

all: $(TARGET)

$(TARGET): $(SOURCES) $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f $(TARGET)
