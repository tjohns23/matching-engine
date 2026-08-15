CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
LDFLAGS := -mconsole -static -static-libgcc -static-libstdc++
SRC := order-book.cpp matching-engine.cpp
TEST_SRC := test.cpp $(SRC)

ifeq ($(OS),Windows_NT)
EXE := order-book.exe
TEST_EXE := test.exe
RM := del /Q
else
EXE := order-book
TEST_EXE := test
RM := rm -f
endif

all: $(TEST_EXE)

$(TEST_EXE): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $(TEST_EXE) $(LDFLAGS)

test: $(TEST_EXE)
	./$(TEST_EXE)

clean:
	$(RM) $(EXE) $(TEST_EXE)

.PHONY: all test clean

