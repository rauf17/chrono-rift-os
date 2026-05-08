# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread -I.
LIBS = -lncurses -lrt

ARBITER_SRCS = $(wildcard arbiter/*.cpp)
HIP_SRCS = $(wildcard hip/*.cpp)
ASP_SRCS = $(wildcard asp/*.cpp)

# Default target builds all executables
all: arbiter/arbiter hip/hip asp/asp

# Compile arbiter executable from its source
arbiter/arbiter: $(ARBITER_SRCS)
	$(CXX) $(CXXFLAGS) -o arbiter/arbiter $(ARBITER_SRCS) $(LIBS)

# Compile hip executable from its source
hip/hip: $(HIP_SRCS)
	$(CXX) $(CXXFLAGS) -o hip/hip $(HIP_SRCS) $(LIBS)

# Compile asp executable from its source
asp/asp: $(ASP_SRCS)
	$(CXX) $(CXXFLAGS) -o asp/asp $(ASP_SRCS) $(LIBS)

# Clean target removes all executables
clean:
	rm -f arbiter/arbiter hip/hip asp/asp