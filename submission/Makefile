# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread
LIBS = -lncurses -lrt

# Default target builds all executables
all: arbiter/arbiter hip/hip asp/asp

# Compile arbiter executable from its source
arbiter/arbiter: arbiter/arbiter.cpp
	$(CXX) $(CXXFLAGS) -o arbiter/arbiter arbiter/arbiter.cpp $(LIBS)

# Compile hip executable from its source
hip/hip: hip/hip.cpp
	$(CXX) $(CXXFLAGS) -o hip/hip hip/hip.cpp $(LIBS)

# Compile asp executable from its source
asp/asp: asp/asp.cpp
	$(CXX) $(CXXFLAGS) -o asp/asp asp/asp.cpp $(LIBS)

# Clean target removes all executables
clean:
	rm -f arbiter/arbiter hip/hip asp/asp