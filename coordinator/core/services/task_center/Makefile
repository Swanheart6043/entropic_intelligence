CC = g++

CFLAGS = \
	-std=c++17 \
	-Wall \
	-DCPPHTTPLIB_OPENSSL_SUPPORT \
	-I../../common/include \
	-I./src

LDFLAGS := \
	-rdynamic \
	-L../../common/lib -lcommon \
	-lINIReader \
	-linih \
	-lcurl \
	-lzmq \
	-lssl \
	-lcrypto \
	-lpthread \
	-lmysqlclient \
	-lmysqlcppconn \
	-lfmt \
	-lspdlog

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = test
TARGET = $(BUILD_DIR)/task_center

# 主程序
SRCS = $(shell find $(SRC_DIR) -name "*.cpp")
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

# 测试程序（不包含 main.cpp）
TEST_TARGET = $(BUILD_DIR)/test_runner
TEST_SRCS = $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.cpp, $(BUILD_DIR)/test_%.o, $(TEST_SRCS))

# 测试需要的源文件（排除 main.cpp）
LIB_SRCS = $(filter-out $(SRC_DIR)/main.cpp, $(SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(LIB_SRCS))

all: $(TARGET)

# 主程序
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 测试程序
test: $(TEST_TARGET)
	@echo "运行测试..."
	@./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS) $(LIB_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run:
	sudo ./$(BUILD_DIR)/task_center

test-zmq: $(TEST_TARGET)
	./$(TEST_TARGET) audio

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean test run