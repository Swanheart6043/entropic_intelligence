CREATE TABLE IF NOT EXISTS common_config (
    id TINYINT UNSIGNED NOT NULL DEFAULT 1,
    fire_alarm_enabled  TINYINT(1) DEFAULT 1,
    temp_alarm_enabled  TINYINT(1) DEFAULT 1,
    humid_alarm_enabled TINYINT(1) DEFAULT 1,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    CONSTRAINT chk_common_single_row CHECK (id = 1)
);

ALTER TABLE common_config
    ADD COLUMN temp_top_x10   SMALLINT NOT NULL DEFAULT 500,   -- 温度上限 50.0℃
    ADD COLUMN temp_low_x10   SMALLINT NOT NULL DEFAULT -200,  -- 温度下限 -20.0℃
    ADD COLUMN humid_top_x10  SMALLINT NOT NULL DEFAULT 900,   -- 湿度上限 90.0%
    ADD COLUMN humid_low_x10  SMALLINT NOT NULL DEFAULT 660,   -- 湿度下限 66.0%
    ADD COLUMN infrared_top_x10 SMALLINT NOT NULL DEFAULT 200; -- 红外上限 20.0℃

-- 幂等 seed: 重跑不报错也不覆盖已有值
INSERT INTO common_config (id) VALUES (1) ON DUPLICATE KEY UPDATE id = id;