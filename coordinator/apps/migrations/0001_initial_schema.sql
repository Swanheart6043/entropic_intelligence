-- 动作表
CREATE TABLE IF NOT EXISTS plans_actions (
    id INT PRIMARY KEY AUTO_INCREMENT,
    action_group_id INT NOT NULL COMMENT '动作组id',
    action_id INT NOT NULL COMMENT '动作id',
    action_type VARCHAR(100) NOT NULL COMMENT '动作类型',
    action_value1 TEXT COMMENT '动作值1',
    action_value2 TEXT COMMENT '动作值2',
    action_value3 TEXT COMMENT '动作值3',
    action_value4 TEXT COMMENT '动作值4',
    action_remark TEXT COMMENT '动作单元名称',
    action_wait TEXT COMMENT '动作间隔时间',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_action_group_id (action_group_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='动作表';

-- 计划点位动作组关联表
CREATE TABLE IF NOT EXISTS plans_points (
    id INT PRIMARY KEY AUTO_INCREMENT,
    point_name VARCHAR(200) NOT NULL COMMENT '点位名称',
    map_name VARCHAR(200) NOT NULL COMMENT '地图名称',
    line_id INT NOT NULL COMMENT '路线ID',
    x DECIMAL(10, 3) NOT NULL COMMENT '点位X坐标',
    y DECIMAL(10, 3) NOT NULL COMMENT '点位Y坐标',
    angle DECIMAL(10, 3) NOT NULL COMMENT '角度',
    action_group_id INT NOT NULL COMMENT '动作组id',
    point_type INT NOT NULL COMMENT '点位类型',
    patrol_speed INT NOT NULL COMMENT '巡逻速度',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_line_id (line_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='计划点位动作组关联表';

-- 巡逻计划表
CREATE TABLE IF NOT EXISTS plans (
    id INT PRIMARY KEY AUTO_INCREMENT,
    plan_id INT NOT NULL COMMENT '计划id',
    plan_name VARCHAR(200) NOT NULL COMMENT '计划名称',
    plan_type VARCHAR(200) NOT NULL COMMENT '计划类型',
    execution_mode VARCHAR(200) NOT NULL COMMENT '执行模式',
    line_id INT NOT NULL COMMENT '路线ID',
    start_time TIME NOT NULL COMMENT '开始时间',
    weekly varchar(200) NOT NULL COMMENT '星期',
    end_time TIME COMMENT '结束时间',
    gap INT COMMENT '执行间隔（分钟）',
    is_finish_soon BOOLEAN DEFAULT FALSE COMMENT '是否到时间结束',
    last_execution_time DATETIME COMMENT '最后执行时间',
    execution_status VARCHAR(200) NOT NULL COMMENT '执行状态',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_plan_name (plan_name),
    INDEX idx_execution_status (execution_status),
    INDEX idx_start_time (start_time),
    INDEX idx_end_time (end_time),
    INDEX idx_line_id (line_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='计划表';

-- 值守计划表
CREATE TABLE IF NOT EXISTS plans_duty (
    id INT PRIMARY KEY AUTO_INCREMENT,
    duty_plan_id INT NOT NULL COMMENT '值守计划id',
    duty_plan_name VARCHAR(200) NOT NULL COMMENT '值守计划名称',
    start_time TIME NOT NULL COMMENT '开始时间',
    end_time TIME COMMENT '结束时间',
    weekly varchar(200) NOT NULL COMMENT '星期',
    gap INT COMMENT '执行间隔（分钟）',
    map_name VARCHAR(200) NOT NULL COMMENT '地图名称',
    point_name VARCHAR(200) NOT NULL COMMENT '点位名称',
    action_group_id INT NOT NULL COMMENT '动作组id',
    last_execution_time DATETIME COMMENT '最后执行时间',
    execution_status VARCHAR(200) NOT NULL COMMENT '执行状态',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_duty_plan_id (duty_plan_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='值守计划表';

-- 预置位
CREATE TABLE IF NOT EXISTS preset_position (
    id INT PRIMARY KEY AUTO_INCREMENT,
    preset_position_code INT COMMENT '预置位号',
    preset_position_name VARCHAR(200) COMMENT '预置位名',
    horizontal_angle DECIMAL(10, 2) NOT NULL COMMENT '水平角度',
    vertical_angle DECIMAL(10, 2) NOT NULL COMMENT '垂直角度',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_preset_position_code (preset_position_code)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='预置位表';
INSERT IGNORE INTO preset_position (preset_position_name, preset_position_code, horizontal_angle, vertical_angle) VALUES 
    ('正前', 001, 359.99, 0.00),
    ('正左', 002, 269.99, 0.00),
    ('正右', 003, 89.99, 0.00),
    ('正后', 004, 179.99, 0.00),
    ('左45', 005, 314.99, 0.00),
    ('右45', 006, 44.99, 0.00),
    ('左30', 007, 344.99, 0.00),
    ('右30', 008, 14.99, 0.00),
    ('左下', 009, 269.99, 17.50),
    ('右下', 010, 89.99, 17.50),
    ('SOS', 011, 359.99, 0.00),
    ('复位', 012, 359.99, 0.00),
    ('后左45', 013, 224.99, 0.00),
    ('后右45', 014, 134.99, 0.00),
    ('前上', 015, 359.99, 308.57),
    ('左上', 016, 269.99, 308.57),
    ('右上', 017, 89.99, 308.57),
    ('充电', 018, 179.99, 0.00);

-- 基础设置表
CREATE TABLE IF NOT EXISTS settings_basic (
    id INT PRIMARY KEY AUTO_INCREMENT,
    config_key VARCHAR(50) UNIQUE NOT NULL,
    config_value VARCHAR(200),
    description VARCHAR(200),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
INSERT IGNORE INTO settings_basic (config_key, config_value, description) VALUES 
    ('mode', 'auto', '自动手动模式'),
    ('doing', '', '正在做'),
    ('head_light', 'off', '大灯'),
    ('warning_light', 'off', '警灯'), 
    ('continuous_wiper', 'off', '连续雨刷'),
    ('volume', '0', '音量'),
    ('low_battery_threshold', '15', '低电量阈值'),
    ('full_charging_threshold', '70', '充电满阈值'),
    ('patrol_or_charging_priority', 'charging', '巡逻优先还是充电优先');

-- 定时任务配置
CREATE TABLE IF NOT EXISTS scheduled_tasks (
    id INT PRIMARY KEY AUTO_INCREMENT,
    task_type VARCHAR(50) COMMENT '定时任务类型',
    task_value VARCHAR(50) COMMENT '值',
    start_time VARCHAR(200) COMMENT '开始时间',
    end_time VARCHAR(200) COMMENT '结束时间',
    last_execution_time DATETIME COMMENT '最后执行时间',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 告警配置
CREATE TABLE IF NOT EXISTS alert_config (
    id INT PRIMARY KEY AUTO_INCREMENT,
    device_id VARCHAR(50) NOT NULL,
    switch_status ENUM('all_day_on', 'all_day_off', 'scheduled') NOT NULL DEFAULT 'all_day_on' COMMENT '全天开/全天关/定时',
    start_time TIME DEFAULT '00:00:00' COMMENT '开始时间',
    end_time TIME DEFAULT '23:59:59' COMMENT '结束时间',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- VDS算法配置
CREATE TABLE IF NOT EXISTS vds_config (
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

-- 安卓服务 - 用户表
CREATE TABLE IF NOT EXISTS android_server_user (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '用户ID',
    username VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
    password VARCHAR(255) NOT NULL COMMENT '密码'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';
INSERT IGNORE INTO android_server_user (username, password) VALUES ('heyn', 'heyn@1688');

-- 安卓服务 - 路线表
CREATE TABLE IF NOT EXISTS android_server_path (
    id INT PRIMARY KEY AUTO_INCREMENT,
    port_number INT,
    sensor_type VARCHAR(50),
    unit VARCHAR(20),
    enabled BOOLEAN DEFAULT TRUE
);

INSERT IGNORE INTO schema_migrations (version) VALUES ('0001');