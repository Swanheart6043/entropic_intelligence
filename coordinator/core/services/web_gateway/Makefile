CXX = g++

CXXFLAGS = \
	-std=c++17 \
	-Wall \
	-DCPPHTTPLIB_OPENSSL_SUPPORT \
	-I../../common/include \
	-I./src \
	-pthread

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
TARGET = $(BUILD_DIR)/web_gateway

SRCS := $(shell find src -name '*.cpp')
OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

run: all
	./$(TARGET)

# Focused regression tests; no robot hardware, database or cloud account needed.
TEST_LDLIBS ?= -lssl -lcrypto -pthread

$(BUILD_DIR)/cloud_outbox_test: tests/cloud_outbox_test.cpp src/cloud_api/cloud_outbox.cpp src/cloud_api/cloud_outbox.hpp ../../common/include/config_parser.hpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/cloud_outbox_test.cpp src/cloud_api/cloud_outbox.cpp -o $@

$(BUILD_DIR)/websocket_send_test: tests/websocket_send_test.cpp src/common/wss_connector.cpp src/common/wss_connector.hpp src/common/websocket/client_ws.hpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tests/websocket_send_test.cpp src/common/wss_connector.cpp $(TEST_LDLIBS) -o $@

test: $(BUILD_DIR)/cloud_outbox_test $(BUILD_DIR)/websocket_send_test
	./$(BUILD_DIR)/cloud_outbox_test
	python3 tests/run_websocket_tests.py ./$(BUILD_DIR)/websocket_send_test

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run test
