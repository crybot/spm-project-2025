CXX := g++
CXXFLAGS := -std=c++23 -O3 -pthread -Wall

# Directory containing the source files
SRCDIR = .

# Find all .cpp files in the source directory
SOURCES = $(wildcard $(SRCDIR)/*.cpp)

# Generate corresponding executable names by removing the .cpp extension
TARGETS = $(patsubst $(SRCDIR)/%.cpp,%,$(SOURCES))

# Phony targets are not actual files
.PHONY: all clean

# The 'all' target will build all executables
all: $(TARGETS)

# Pattern rule to compile each .cpp file into an executable
# $< refers to the first prerequisite (the .cpp file)
# $@ refers to the target (the executable file)
%: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

# The 'clean' target will remove all compiled executables
clean:
	rm -f $(TARGETS)
