CPP_FILES := $(wildcard src/*.cpp)
C_FILES := $(wildcard src/*.c)
CPP_OBJ := $(patsubst src/%.cpp, obj/%.o, $(CPP_FILES))
C_OBJ := $(patsubst src/%.c, obj/%.o, $(C_FILES))
LD_FLAGS := -pthread
CC_FLAGS := 
CC := gcc
CXX := g++
BUILD_NUMBER_FILE := build-number.txt

all: LD_FLAGS += -static-libgcc -static-libstdc++
all: CC_FLAGS := $(CC_FLAGS) -D__BUILD_NUMBER=$(shell cat $(BUILD_NUMBER_FILE))
all: ntfs-linker.exe $(BUILD_NUMBER_FILE)

debug: CC_FLAGS := $(CC_FLAGS) -g
debug: CC_FLAGS := $(CC_FLAGS) -D__BUILD_NUMBER=0
debug: LD_FLAGS += -g
debug: ntfs-linker.exe

ntfs-linker.exe: $(CPP_OBJ) $(C_OBJ)
	$(CXX) $(LD_FLAGS) -o $@ $^

obj/%.o: src/%.cpp
	$(CXX) $(CC_FLAGS) -c -o $@ $<
obj/%.o: src/%.c
	$(CC) $(CC_FLAGS) -c -o $@ $<

include buildnumber.mak
