CREATE TABLE IF NOT EXISTS battery_config (
    id TINYINT UNSIGNED NOT NULL DEFAULT 1,
    battery_low_pct TINYINT UNSIGNED NOT NULL DEFAULT 20, -- 低电量阈值 %
    battery_high_pct TINYINT UNSIGNED NOT NULL DEFAULT 80, -- 充电满阈值 %
    charge_strategy ENUM('patrol_first','charge_first') NOT NULL DEFAULT 'charge_first', -- 充电优先等
    battery_temp_warn_c SMALLINT NOT NULL DEFAULT 60,
    battery_alarm_enabled TINYINT(1) NOT NULL DEFAULT 1,
    battery_temp_enabled TINYINT(1) NOT NULL DEFAULT 1,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    CONSTRAINT chk_charging_single_row CHECK (id = 1),
    CONSTRAINT chk_low_range CHECK (battery_low_pct   BETWEEN 0 AND 100),
    CONSTRAINT chk_full_range CHECK (battery_high_pct BETWEEN 0 AND 100),
    CONSTRAINT chk_low_lt_full CHECK (battery_low_pct < battery_high_pct)
);

ALTER TABLE battery_config
    ADD COLUMN charge_end_patrol TINYINT(1) NOT NULL DEFAULT 0
    AFTER charge_strategy;

-- 幂等 seed: 重跑不报错也不覆盖已有值
INSERT INTO battery_config (id) VALUES (1) ON DUPLICATE KEY UPDATE id = id;