CXX = g++

CXXFLAGS = \
	-std=c++17 \
	-Wall \
	-I../../common/include \
	-I./src \
	-I./include \
	-pthread \

LDFLAGS := \
	-L../../common/lib \
	-lcommon \
	-lINIReader \
	-linih \
	-lcurl \
	-lzmq \
	-lssl \
	-lcrypto \
	-lpthread \
	-lmysqlclient \
	-lmysqlcppconn

BUILD_DIR = build
TARGET = $(BUILD_DIR)/edge_gateway

SRCS := $(shell find src -name '*.cpp')
OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run