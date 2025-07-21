CC = arm-none-linux-gnueabi-gcc
CFLAGS = -Wall -I./src -I.src/utils
SRC_DIR = src
UTILS_DIR = src/utils
TEST_DIR = test
BUILD_DIR = build
TARGET = $(BUILD_DIR)/warm_home_app
TEST_TARGET = $(BUILD_DIR)/warm_home_app_test

# 搜索src目录下所有.c文件（排除utils目录）
SRCS = $(filter-out $(UTILS_DIR)/%.c, $(wildcard $(SRC_DIR)/*.c))
# 搜索utils目录下所有.c文件
UTILS_SRCS = $(wildcard $(UTILS_DIR)/*.c)
# 合并所有源文件
ALL_SRCS = $(SRCS) $(UTILS_SRCS)

# 生成对象文件路径
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
UTILS_OBJS = $(patsubst $(UTILS_DIR)/%.c, $(BUILD_DIR)/utils_%.o, $(UTILS_SRCS))
ALL_OBJS = $(OBJS) $(UTILS_OBJS)

# 测试源文件
TEST_SRCS = $(TEST_DIR)/main.c
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/test_%.o, $(TEST_SRCS))

# 默认目标
all: $(TARGET)

# 测试目标
test: $(TEST_TARGET)

# 链接目标
$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# 链接测试目标
$(TEST_TARGET): $(TEST_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# 编译src目录下的 .c 文件到 .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译utils目录下的 .c 文件到 .o（添加utils_前缀避免冲突）
$(BUILD_DIR)/utils_%.o: $(UTILS_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译test目录下的 .c 文件到 .o（添加test_前缀避免冲突）
$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean

deploy: all
	sudo cp $(TARGET) $(TEST_TARGET) ~/nfs/4412/warm_home
	@echo "Deploy done."