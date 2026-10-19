CC = g++

CFLAGS = \
	-std=c++17 \
	-Wall \
	-I../../common/include \
	-I./src \
	$(shell pkg-config --cflags libavformat libavcodec libavutil libavdevice libswscale libswresample libavfilter)

LDFLAGS := \
	-L../../common/lib -lcommon \
	-lINIReader \
	-linih \
	$(shell pkg-config --libs libavformat libavcodec libavutil libavdevice libswscale libswresample libavfilter) \
	-lzmq \
	-lssl \
	-lcrypto \
	-lpthread

SRC_DIR = src
BUILD_DIR = build
TEST_DIR = test
TARGET = $(BUILD_DIR)/audio_video_center

# 主程序
TARGET = $(BUILD_DIR)/audio_video_center
SRCS = $(shell find $(SRC_DIR) -name '*.cpp')
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

# 测试程序（不包含 main.cpp）
TEST_TARGET = $(BUILD_DIR)/test_audio_video_center
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

run: $(TARGET)
	./$(TARGET)

test-streamer: $(TEST_TARGET)
	./$(TEST_TARGET) streamer

test-capture: $(TEST_TARGET)
	./$(TEST_TARGET) capture

test-zmq: $(TEST_TARGET)
	./$(TEST_TARGET) zmq

test-integration: $(TEST_TARGET)
	./$(TEST_TARGET) integration

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean test run