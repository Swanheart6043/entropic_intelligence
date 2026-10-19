CREATE TABLE IF NOT EXISTS alarm_rule_config (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    rule_code VARCHAR(64) NOT NULL COMMENT '规则编码，如 fire、temperature、obstacle',
    rule_name VARCHAR(64) NOT NULL COMMENT '规则名称',
    enabled TINYINT(1) NOT NULL DEFAULT 0 COMMENT '是否启用',
    detect_interval_ms INT UNSIGNED NOT NULL DEFAULT 1000 COMMENT '检测周期，毫秒',
    trigger_duration_ms INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目标持续出现多久后触发，毫秒',
    alarm_cooldown_ms INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '同类告警重复触发冷却时间，毫秒',
    rule_params JSON DEFAULT NULL COMMENT '该告警独有的检测参数',
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_rule_code (rule_code)
);