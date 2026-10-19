ALTER TABLE algorithm_config
    CHANGE COLUMN detect_type algorithm_type INT NOT NULL COMMENT '算法类型(对应 Type)',
    CHANGE COLUMN camera_name algorithm_name VARCHAR(100) NOT NULL COMMENT '算法名称(对应 Name)',
    MODIFY COLUMN camera_channel INT NOT NULL COMMENT '摄像头通道(对应 Channel)',
    CHANGE COLUMN enable_type time_ranges JSON NOT NULL COMMENT '每日启用时间段数组(对应 Time.time)',
    MODIFY COLUMN start_date DATE NOT NULL COMMENT '生效开始日期(对应 StartDate)',
    MODIFY COLUMN end_date DATE NOT NULL COMMENT '生效结束日期(对应 EndDate)',
    MODIFY COLUMN is_charging_off TINYINT(1) NOT NULL DEFAULT 0 COMMENT '按区域启用标志(对应 ByRegion)',
    MODIFY COLUMN is_enabled TINYINT(1) NOT NULL DEFAULT 1 COMMENT '是否启用(对应 Enable)',
    ADD COLUMN updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP AFTER created_at;
