# Makefile for serverDemo.cpp

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -g

# Libraries to link
LIBS = -lpthread

# Source file
SRCS = serverDemo.cpp

# Executable name
TARGET = server

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
