MODULES := web_gateway task_center monitor_center edge_gateway audio_video_center
SERVICE_DIR := services

VERSION ?= $(shell git describe --tags --always --dirty)
BUILD_DIR := build/patrol_robot_h1
TIMESTAMP := $(shell date +%Y%m%d_%H%M%S)

.PHONY: all clean package common $(MODULES)

all: common $(MODULES)

common:
	@echo ">>> Building common ..."
	$(MAKE) -C common

$(MODULES): common
	@echo ">>> Building $@ ..."
	$(MAKE) -C $(SERVICE_DIR)/$@

clean:
	$(MAKE) -C common clean
	@for mod in $(MODULES); do $(MAKE) -C $(SERVICE_DIR)/$$mod clean; done
	rm -rf build/

# 构建完后组装目标目录结构
package: all
	@echo ">>> Packaging $(VERSION) ..."
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/logs $(BUILD_DIR)/crashes

	# 复制各进程产物
	@for mod in $(MODULES); do \
    mkdir -p $(BUILD_DIR)/$$mod; \
    test -d $(SERVICE_DIR)/$$mod/build || { echo "ERROR: $(SERVICE_DIR)/$$mod/build not found"; exit 1; }; \
    cp -r $(SERVICE_DIR)/$$mod/build/* $(BUILD_DIR)/$$mod/; \
	done

	# 复制 配置、脚本、systemd
	cp -r scripts $(BUILD_DIR)/
	cp -r systemds $(BUILD_DIR)/
	cp configs/config.ini $(BUILD_DIR)/
	echo "$(VERSION) $(TIMESTAMP)" > $(BUILD_DIR)/version.txt
	
	# 打包
	cd build && tar czf patrol_robot_h1_$(VERSION)_$(TIMESTAMP).tar.gz patrol_robot_h1/
	@echo ">>> Package: build/patrol_robot_h1_$(VERSION)_$(TIMESTAMP).tar.gz"
