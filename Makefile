# Makefile for the Scalable Parallel External-Memory Sort project
#
# This Makefile compiles and links C++ sources using the OpenMPI
# wrapper (mpic++), which also allows compiling OpenMP and standard C++ code.

# -----------------------------------------------------------------------------
# Compiler and Flags
# -----------------------------------------------------------------------------
# Use the OpenMPI C++ compiler wrapper. It will use g++ underneath but
# automatically adds all necessary MPI include and library paths.
CXX = mpic++

# Base CXXFLAGS for all files
CXXFLAGS = -std=c++20 -Iinclude -Ifastflow -Wall -Wextra -O3 -g -MMD -MP

# Linker flags. -pthread is required by FastFlow.
LDFLAGS = -pthread

# Flags for specific dependencies
OMP_FLAGS = -fopenmp
FF_LIBS =

# Create a specific set of flags for compiling OpenMP code
OMP_CXXFLAGS = $(CXXFLAGS) $(OMP_FLAGS)

# -----------------------------------------------------------------------------
# Directories and Files
# -----------------------------------------------------------------------------
BUILD_DIR = build_make

# Source files are found automatically, including the new MPI app
SRCS = $(wildcard src/*.cpp) \
       $(wildcard apps/ff_single_node/*.cpp) \
       $(wildcard apps/omp_single_node/*.cpp) \
       $(wildcard apps/mpi_omp_multi_node/*.cpp) \
       $(wildcard tools/*.cpp) \
       $(wildcard tests/*.cpp) \
       $(wildcard examples/*.cpp)

# Object files
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(patsubst %.cpp,$(BUILD_DIR)/%.d,$(SRCS))
COMMON_OBJS = $(BUILD_DIR)/src/utils.o $(BUILD_DIR)/src/record_loader.o

# Specific Object files that need OpenMP
OMP_APP_OBJ = $(BUILD_DIR)/apps/omp_single_node/main.o
OMP_EXAMPLE_OBJ = $(BUILD_DIR)/examples/omp_example.o

# -----------------------------------------------------------------------------
# Executable Targets
# -----------------------------------------------------------------------------
# Tools
CREATE_FILE_EXE = $(BUILD_DIR)/tools/create_file
READ_FILE_EXE = $(BUILD_DIR)/tools/read_file
# Applications
FF_APP_EXE = $(BUILD_DIR)/apps/ff_single_node/ff_single_node
OMP_APP_EXE = $(BUILD_DIR)/apps/omp_single_node/omp_single_node
MPI_APP_EXE = $(BUILD_DIR)/apps/mpi_omp_multi_node/mpi_omp_multi_node # New MPI target
# Tests
TEST_EXE = $(BUILD_DIR)/tests/memory_arena_test
# Examples
FF_EXAMPLE_EXES = $(BUILD_DIR)/examples/farm_example \
                  $(BUILD_DIR)/examples/farm_pattern_example \
                  $(BUILD_DIR)/examples/map_reduce_example \
                  $(BUILD_DIR)/examples/template_example
OMP_EXAMPLE_EXES = $(BUILD_DIR)/examples/omp_example

# Add the new MPI executable to the list of all targets
TARGETS = $(CREATE_FILE_EXE) $(READ_FILE_EXE) $(FF_APP_EXE) $(OMP_APP_EXE) $(MPI_APP_EXE) $(TEST_EXE) $(FF_EXAMPLE_EXES) $(OMP_EXAMPLE_EXES)

# -----------------------------------------------------------------------------
# Main Rules
# -----------------------------------------------------------------------------
.PHONY: all clean

all: $(TARGETS)

clean:
	@echo "Cleaning up..."
	@rm -rf $(BUILD_DIR)

# -----------------------------------------------------------------------------
# Linking Rules
# -----------------------------------------------------------------------------
# New rule for linking the MPI application
$(MPI_APP_EXE): $(BUILD_DIR)/apps/mpi_omp_multi_node/main.o $(COMMON_OBJS)
	@echo "Linking (MPI) $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ $(OMP_FLAGS) -o $@

$(FF_APP_EXE): $(BUILD_DIR)/apps/ff_single_node/main.o $(COMMON_OBJS)
	@echo "Linking $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ $(FF_LIBS) -o $@

$(OMP_APP_EXE): $(OMP_APP_OBJ) $(COMMON_OBJS)
	@echo "Linking (OMP) $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ $(OMP_FLAGS) -o $@

$(CREATE_FILE_EXE): $(BUILD_DIR)/tools/create_file.o $(COMMON_OBJS)
	@echo "Linking $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@

$(READ_FILE_EXE): $(BUILD_DIR)/tools/read_file.o $(COMMON_OBJS)
	@echo "Linking $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@

$(TEST_EXE): $(BUILD_DIR)/tests/memory_arena_test.o
	@echo "Linking $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@

$(FF_EXAMPLE_EXES): $(BUILD_DIR)/examples/%: $(BUILD_DIR)/examples/%.o
	@echo "Linking $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ $(FF_LIBS) -o $@

$(OMP_EXAMPLE_EXES): $(OMP_EXAMPLE_OBJ)
	@echo "Linking (OMP) $@..."
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ $(OMP_FLAGS) -o $@

# -----------------------------------------------------------------------------
# Compilation Rules
# -----------------------------------------------------------------------------

# General rule for compiling non-OpenMP .cpp files
$(BUILD_DIR)/%.o: %.cpp
	@echo "Compiling $<..."
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OMP_CXXFLAGS) -c $< -o $@

# Specific rules to compile OpenMP files using the OMP_CXXFLAGS
$(OMP_APP_OBJ): apps/omp_single_node/main.cpp
	@echo "Compiling (OMP) $<..."
	@mkdir -p $(@D)
	$(CXX) $(OMP_CXXFLAGS) -c $< -o $@

$(OMP_EXAMPLE_OBJ): examples/omp_example.cpp
	@echo "Compiling (OMP) $<..."
	@mkdir -p $(@D)
	$(CXX) $(OMP_CXXFLAGS) -c $< -o $@

# -----------------------------------------------------------------------------
# Dependency Management
# -----------------------------------------------------------------------------
-include $(DEPS)
