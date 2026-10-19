CREATE TABLE IF NOT EXISTS alarm_action_config (
    id              BIGINT AUTO_INCREMENT PRIMARY KEY,
    alarm_type      VARCHAR(64)  NOT NULL,  -- 例如 433
    action_id       VARCHAR(64)  NOT NULL,  -- 例如 424
    action_name     VARCHAR(128) NOT NULL,  -- 例如 灯光
    action_count    INT UNSIGNED NOT NULL,  -- 云端原始 ActionCount
    unit_order      INT UNSIGNED NOT NULL,  -- ActionUnit 数组下标，保证执行顺序
    action_type     VARCHAR(64)  NOT NULL,
    action_value1   VARCHAR(255) NOT NULL,
    action_value2   VARCHAR(255) NOT NULL,
    action_wait_ms  INT UNSIGNED NOT NULL DEFAULT 0,
    updated_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_alarm_action_unit (alarm_type, action_id, unit_order),
    KEY idx_alarm_execute (alarm_type, unit_order)
) COMMENT='云端下发的机器人告警处置配置';