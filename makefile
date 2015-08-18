CPP_FILES := $(wildcard src/*.cpp)
C_FILES := $(wildcard src/*.c)
CPP_OBJ := $(patsubst src/%.cpp, obj/%.o, $(CPP_FILES))
C_OBJ := $(patsubst src/%.c, obj/%.o, $(C_FILES))
LD_FLAGS := -pthread
CC_FLAGS :=
CXX_FLAGS := -std=c++11
CC := gcc
CXX := g++
BUILD_NUMBER_FILE := build-number.txt

all: LD_FLAGS += -static-libgcc -static-libstdc++
all: CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=$(shell cat $(BUILD_NUMBER_FILE))
all: ntfs-linker $(BUILD_NUMBER_FILE)

debug: CXX_FLAGS := $(CXX_FLAGS) -g
debug: CXX_FLAGS := $(CXX_FLAGS) -D__BUILD_NUMBER=0
debug: LD_FLAGS += -g
debug: ntfs-linker

ntfs-linker: $(CPP_OBJ) $(C_OBJ)
	$(CXX) $(LD_FLAGS) -o $@ $^ -ldl

obj/%.o: src/%.cpp
	$(CXX) $(CXX_FLAGS) -c -o $@ $<
obj/%.o: src/%.c
	$(CC) $(CC_FLAGS) -c -o $@ $<

include buildnumber.mak
