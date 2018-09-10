
# Which C++ compiler to use. This works for both Clang and G++.
CXX = c++

# Location of protobuf include files, library, and compiler.
PROTOBUF_DIR = /opt/local

CXX_FLAGS = -Wfatal-errors -Wall -Wextra -Wpedantic -Wshadow -std=c++11 -O3 -ffast-math -I$(PROTOBUF_DIR)/include

# Unfortunately protobuf's generated C++ code is pretty crappy, so we have to turn
# off a bunch of warnings globally.
CXX_FLAGS += -Wno-nested-anon-types -Wno-unused-parameter -Wno-deprecated-declarations -Wno-sign-compare

# We stick everything in this directory. Easier to ignore (in git) and clean up.
BUILD_DIR = build

# The binary we build.
BIN = $(BUILD_DIR)/distrend

# Collect all the source files we compile.
CPP = $(wildcard *.cpp)
OBJ = $(CPP:%.cpp=$(BUILD_DIR)/%.o)
DEP = $(OBJ:%.o=%.d)
LIBS = -lm -lpthread -lprotobuf
LDFLAGS = -L$(PROTOBUF_DIR)/lib
BIN_DEPS =

$(BIN): $(OBJ) $(BIN_DEPS) Makefile
	mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS) $(LDFLAGS) $(OBJ) -o $(BIN) $(LIBS)

-include $(DEP)

$(BUILD_DIR)/%.o: %.cpp Makefile
	mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS) -MMD -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

