CREATE TABLE IF NOT EXISTS algorithm_config (
    id INT PRIMARY KEY AUTO_INCREMENT,
    detect_type VARCHAR(100) COMMENT '检测类型',
    camera_name VARCHAR(100) COMMENT '摄像头名称',
    camera_channel VARCHAR(100) COMMENT '摄像头通道',
    enable_type VARCHAR(20) COMMENT '开启类型',
    start_date VARCHAR(20) COMMENT '开始日期',
    end_date VARCHAR(20) COMMENT '结束日期',
    is_charging_off VARCHAR(100) COMMENT '充电是否关闭',
    is_enabled VARCHAR(100) COMMENT '是否启用',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);