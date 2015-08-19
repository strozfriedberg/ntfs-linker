CPP_FILES := $(wildcard src/*.cpp)
C_FILES := $(wildcard src/*.c)
CPP_OBJ := $(patsubst src/%.cpp, build/%.o, $(CPP_FILES))
C_OBJ := $(patsubst src/%.c, build/%.o, $(C_FILES))
INCLUDES = -I include/ -I include/utf8 -I include/sqlite3
LD_FLAGS := -pthread
CC_FLAGS :=
CXX_FLAGS := -std=c++11 -Wall $(INCLUDES)
CC := gcc
CXX := g++
BUILD_NUMBER_FILE := build-number.txt
OBJS := $(C_OBJ) $(CPP_OBJ)
DEPS := $(OBJS:.o=.d)
BIN := ntfs-linker

all: LD_FLAGS += -static-libgcc -static-libstdc++
all: CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=$(shell cat $(BUILD_NUMBER_FILE))
all: $(BIN) $(BUILD_NUMBER_FILE)

clean: 
	$(RM) $(OBJS) $(DEPS) $(BIN)

rebuild: clean all

debug: CXX_FLAGS := $(CXX_FLAGS) -g
debug: CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=0
debug: LD_FLAGS += -g
debug: $(BIN)

$(BIN): $(CPP_OBJ) $(C_OBJ)
	$(CXX) $(LD_FLAGS) -o build/$@ $^ -ldl

build/%.o: src/%.cpp
	$(CXX) $(CXX_FLAGS) -c -MMD -MP -o $@ $<
build/%.o: src/%.c
	$(CC) $(CC_FLAGS) -c -MMD -MP -o $@ $<

include buildnumber.mak
-include $(DEPS)
