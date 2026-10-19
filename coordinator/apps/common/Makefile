CC = g++
CFLAGS = -std=c++17 -Wall -DCPPHTTPLIB_OPENSSL_SUPPORT -g -I./include -rdynamic

SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib
TARGET = $(LIB_DIR)/libcommon.a

SRCS = $(shell find $(SRC_DIR) -name "*.cpp")
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(LIB_DIR)

.PHONY: all clean