CPP_FILES := $(wildcard src/*.cpp)
TEST_FILES := $(wildcard test/*.cpp)
CPP_FILES_COMMON := $(filter-out src/ntfs_linker.cpp, $(CPP_FILES))
CPP_FILES_COMMON := $(filter-out test/lnis.cpp, $(CPP_FILES_COMMON))
C_FILES := $(wildcard src/*.c)
TEST_OBJ := $(patsubst test/%.cpp, build/%.o, $(TEST_FILES))
CPP_OBJ:= $(patsubst src/%.cpp, build/%.o, $(CPP_FILES))
CPP_OBJ_COMMON := $(patsubst src/%.cpp, build/%.o, $(CPP_FILES_COMMON))
C_OBJ := $(patsubst src/%.c, build/%.o, $(C_FILES))
TEST_OBJ := $(patsubst test/%.cpp, build/%.o, $(TEST_FILES))
INCLUDES = -I include/ -I include/utf8 -I include/sqlite3
LD_FLAGS := -pthread
CC_FLAGS :=
CXX_FLAGS := -std=c++11 -Wall $(INCLUDES)
CC := gcc
CXX := g++
BUILD_NUMBER_FILE := build-number.txt
OBJS := $(C_OBJ) $(CPP_OBJ) $(TEST_OBJ)
DEPS := $(OBJS:.o=.d)
BIN := ntfs-linker
LD_FLAGS += -g
CXX_FLAGS := $(CXX_FLAGS) -g -pg

all: LD_FLAGS += -static-libgcc -static-libstdc++
CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=$(shell cat $(BUILD_NUMBER_FILE))
all: $(BIN) $(BUILD_NUMBER_FILE) test

test: $(CPP_OBJ_COMMON) $(C_OBJ) $(TEST_OBJ)
	$(CXX) $(LD_FLAGS) -o build/$@ $^ -ldl

clean:
	$(RM) $(OBJS) $(DEPS) $(BIN)

rebuild: clean all

debug: CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=0
debug: $(BIN)

$(BIN): $(CPP_OBJ_COMMON) $(C_OBJ) build/ntfs_linker.o
	$(CXX) $(LD_FLAGS) -o build/$@ $^ -ldl

build/%.o: src/%.cpp
	$(CXX) $(CXX_FLAGS) -c -MMD -MP -o $@ $<
build/%.o: src/%.c
	$(CC) $(CC_FLAGS) -c -MMD -MP -o $@ $<
build/%.o: test/%.cpp
	$(CXX) $(CXX_FLAGS) -c -MMD -MP -o $@ $<

include buildnumber.mak
-include $(DEPS)
