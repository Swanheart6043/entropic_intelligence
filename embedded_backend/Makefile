SYSROOT := /home/swanheart/codes/rootfs

CC = arm-none-linux-gnueabi-gcc

CFLAGS = \
	-std=c99 \
	-Wall -Wextra -Werror \
    -Wmissing-prototypes -Wmissing-declarations \
    -Wformat=2 -Wformat-security \
    -O1 -g
CFLAGS += \
	-I./include \
	-D_GNU_SOURCE \
	-D_POSIX_C_SOURCE=200809L

LDFLAGS := -static

SRC_DIR = src
UTILS_DIR = src/utils
CJSON_DIR = src/cjson
MONGOOSE_DIR = src/mongoose
BUILD_DIR = build
TARGET = $(BUILD_DIR)/warm_home_backend

# 搜索src目录下的.c文件（排除utils cjson mongoose子目录）
MAIN_SRCS = $(filter-out $(UTILS_DIR)/%.c $(CJSON_DIR)/%.c $(MONGOOSE_DIR)/%.c, $(wildcard $(SRC_DIR)/*.c))
# 搜索公共库文件
UTILS_SRCS = $(wildcard $(UTILS_DIR)/*.c)
CJSON_SRCS = $(wildcard $(CJSON_DIR)/*.c)
MONGOOSE_SRCS = $(wildcard $(MONGOOSE_DIR)/*.c)
# 合并所有源文件
ALL_SRCS = $(MAIN_SRCS) $(UTILS_SRCS) $(CJSON_SRCS) $(MONGOOSE_SRCS)

# 生成对象文件路径
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(MAIN_SRCS))
UTILS_OBJS = $(patsubst $(UTILS_DIR)/%.c, $(BUILD_DIR)/utils_%.o, $(UTILS_SRCS))
CJSON_OBJS = $(patsubst $(CJSON_DIR)/%.c, $(BUILD_DIR)/cjson_%.o, $(CJSON_SRCS))
MONGOOSE_OBJS = $(patsubst $(MONGOOSE_DIR)/%.c, $(BUILD_DIR)/mongoose_%.o, $(MONGOOSE_SRCS))
ALL_OBJS = $(OBJS) $(UTILS_OBJS) $(CJSON_OBJS) $(MONGOOSE_OBJS)

# 默认目标
all: $(TARGET)

# 链接目标
$(TARGET): $(ALL_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# 编译src目录下的 .c 文件到 .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译utils目录下的源文件
$(BUILD_DIR)/utils_%.o: $(UTILS_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译cjson目录下的源文件
$(BUILD_DIR)/cjson_%.o: $(CJSON_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译mongoose目录下的源文件
$(BUILD_DIR)/mongoose_%.o: $(MONGOOSE_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean

deploy: all
	sudo cp $(TARGET) ~/nfs/4412/warm_home
	@echo "Deploy done."