# SpaceTime — host-side unit tests (no Rack SDK).
# Builds against dsp/ only; runs on macOS and Ubuntu.

CXX ?= c++
CXXFLAGS += -std=c++11 -Wall -Wextra -O1 -g
CXXFLAGS += -I../dsp -Ivendor

SOURCES := $(wildcard *.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
TARGET := run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp $(wildcard ../dsp/*.hpp) $(wildcard *.hpp)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all test clean
