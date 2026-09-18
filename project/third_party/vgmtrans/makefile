# Standalone vgmtrans makefile for nintoolbox
PROJECT_NAME = vgmtrans

CC ?= gcc
CXX ?= g++
AR ?= ar
STRIP ?= strip

SRC_DIR = src
BIN_DIR = bin
OBJ_DIR = build

COMMON_FLAGS = -I$(SRC_DIR)/main -I$(SRC_DIR)/main/formats -I$(SRC_DIR)/main/loaders \
               -I$(SRC_DIR)/ui/cli -I$(SRC_DIR)/ui/qt -Ilib/tinyxml -Ilib/minizip \
               -Ilib/zlib/include -DTIXML_USE_STL -DIOAPI_NO_64 -w

CXXFLAGS += -std=c++11 $(COMMON_FLAGS) $(XFLAGS)
CFLAGS += $(COMMON_FLAGS) $(XFLAGS)
LDFLAGS += $(XFLAGS)

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows_NT)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    IS_WIN = 1
endif
ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
    IS_WIN = 1
endif
ifeq ($(OS),Windows_NT)
    IS_WIN = 1
endif
ifneq ($(findstring mingw,$(CC)),)
    IS_WIN = 1
endif
ifneq ($(findstring mingw,$(CXX)),)
    IS_WIN = 1
endif

ifeq ($(IS_WIN),1)
    TARGET = $(BIN_DIR)/$(PROJECT_NAME).exe
    LIBS = -lz
else
    TARGET = $(BIN_DIR)/$(PROJECT_NAME)
    LIBS = -lz
endif

CPP_SRCS = $(wildcard $(SRC_DIR)/main/*.cpp) \
           $(wildcard $(SRC_DIR)/main/formats/*.cpp) \
           $(wildcard $(SRC_DIR)/main/loaders/*.cpp) \
           $(wildcard lib/tinyxml/src/*.cpp) \
           $(SRC_DIR)/ui/cli/CLIVGMRoot.cpp \
           $(SRC_DIR)/ui/cli/main.cpp \
           $(SRC_DIR)/ui/qt/osdepend.cpp

C_SRCS = $(wildcard lib/minizip/*.c)

CPP_OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CPP_SRCS))
C_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SRCS))

ALL_OBJS = $(CPP_OBJS) $(C_OBJS)

all: $(TARGET)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(ALL_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) $(ALL_OBJS) $(LIBS) -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
