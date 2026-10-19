CC = g++

CFLAGS = \
	-std=c++17 \
	-Wall \
	-DCPPHTTPLIB_OPENSSL_SUPPORT \
	-I../../common/include \
	-I./src

LDFLAGS := \
	-L../../common/lib -lcommon \
	-lINIReader \
	-linih \
	-lzmq \
	-lssl \
	-lcrypto \
	-lpthread \
	-lmysqlcppconn \
	-lfmt \
	-lspdlog

SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/monitor_center

SRCS = $(shell find $(SRC_DIR) -name "*.cpp")
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run:
	./$(BUILD_DIR)/monitor_center

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean