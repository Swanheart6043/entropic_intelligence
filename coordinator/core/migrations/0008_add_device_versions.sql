CREATE TABLE IF NOT EXISTS device_versions (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    device_type   VARCHAR(64)  NOT NULL,   -- 'screen' / 'ai_board' / 'control_board' ...
    device_name   VARCHAR(128) DEFAULT NULL, -- 可选：具体设备标识/序列号
    sw_version    VARCHAR(64)  NOT NULL,
    hw_version    VARCHAR(64)  DEFAULT NULL, -- 顺带存硬件版本也方便
    updated_at    TIMESTAMP    DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_device_type (device_type)  -- 每种设备一行，OTA 时 UPSERT
);