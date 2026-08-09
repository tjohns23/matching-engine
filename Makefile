CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
LDFLAGS := -mconsole
SRC := order-book.cpp

ifeq ($(OS),Windows_NT)
EXE := order-book.exe
RM := del /Q
else
EXE := order-book
RM := rm -f
endif

all: $(EXE)

$(EXE): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(EXE) $(LDFLAGS)

clean:
	$(RM) $(EXE)

.PHONY: all clean
