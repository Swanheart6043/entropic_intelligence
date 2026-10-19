-- 1. lang 字段：不存在才添加
SET @sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE broadcast_template ADD COLUMN lang VARCHAR(8) NOT NULL DEFAULT ''zh'' AFTER event_key',
        'SELECT 1'
    )
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'broadcast_template'
      AND COLUMN_NAME = 'lang'
);

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 2. 删除旧的 event_key 单列索引：存在才删除
SET @sql = (
    SELECT IF(
        COUNT(*) > 0,
        'ALTER TABLE broadcast_template DROP INDEX event_key',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'broadcast_template'
      AND INDEX_NAME = 'event_key'
);

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 3. 添加 event_key + lang 唯一索引：不存在才添加
SET @sql = (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE broadcast_template ADD UNIQUE KEY uk_event_lang (event_key, lang)',
        'SELECT 1'
    )
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'broadcast_template'
      AND INDEX_NAME = 'uk_event_lang'
);

PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 4. 幂等插入/更新播报模板
INSERT INTO broadcast_template (event_key, lang, content, enabled, description, created_at, updated_at) VALUES
    ('lidar.someone_on_top', 'en', 'Warning: please do not lean on the vehicle to avoid danger', 1, 'lidar detected someone leaning on the vehicle', NOW(), NOW()),

    ('lidar.obstacle_at_front', 'zh', '请让一让，我要去前面工作啦', 1, '前面有障碍物时', NOW(), NOW()),
    ('lidar.obstacle_at_front', 'en', 'Please make way, I need to get to work in the front', 1, 'When there is an obstacle in front', NOW(), NOW()),

    ('sos.short_press.greeting', 'zh', '您好，如需帮助，请长按s o s按键3秒以上', 1, 'SOS短按问候', NOW(), NOW()),
    ('sos.short_press.greeting', 'en', 'Hello, press and hold the SOS button for over 3 seconds if you need help', 1, 'SOS short press greeting', NOW(), NOW()),

    ('sos.long_press.connecting', 'zh', '正在接通请稍后', 1, 'SOS长按接通中', NOW(), NOW()),
    ('sos.long_press.connecting', 'en', 'Connecting, please wait', 1, 'SOS long press connecting', NOW(), NOW()),

    ('location.lost', 'zh', '定位丢失, 请手动初始化', 1, '定位丢失', NOW(), NOW()),
    ('location.lost', 'en', 'Location lost, please initialize manually', 1, 'Location lost', NOW(), NOW()),

    ('patrol.start', 'zh', '开始巡逻', 1, '开始巡逻', NOW(), NOW()),
    ('patrol.start', 'en', 'Start patrol', 1, 'Start patrol', NOW(), NOW()),

    ('duty.start', 'zh', '开始值守', 1, '开始值守', NOW(), NOW()),
    ('duty.start', 'en', 'Start duty', 1, 'Start duty', NOW(), NOW()),

    ('mode.manual', 'zh', '切换到手动模式', 1, '手动模式', NOW(), NOW()),
    ('mode.manual', 'en', 'Switch to manual mode', 1, 'Manual mode', NOW(), NOW()),

    ('mode.automatic', 'zh', '切换到自动模式', 1, '自动模式', NOW(), NOW()),
    ('mode.automatic', 'en', 'Switch to automatic mode', 1, 'Automatic mode', NOW(), NOW()),

    ('charge.start.human_operate', 'zh', '开始执行一键充电任务', 1, '开始充电，人工操作', NOW(), NOW()),
    ('charge.start.human_operate', 'en', 'Starting one-tap charging task', 1, 'Start charging, manual operation', NOW(), NOW()),

    ('charge.start.after_patrol', 'zh', '任务完成，开始返回充电', 1, '开始充电，巡逻结束', NOW(), NOW()),
    ('charge.start.after_patrol', 'en', 'Task completed, returning to charge', 1, 'Start charging after patrol', NOW(), NOW()),

    ('charge.start.low_battery', 'zh', '电量低，开始返回充电', 1, '开始充电，电量低', NOW(), NOW()),
    ('charge.start.low_battery', 'en', 'Low battery, returning to charge', 1, 'Start charging, low battery', NOW(), NOW()),

    ('charge.start.not_charge_point', 'zh', '当前地图没有充电点，请提前录制充电点', 1, '无充电点时播报', NOW(), NOW()),
    ('charge.start.not_charge_point', 'en', 'No charging point exists on the current map. Please record a charging point in advance.', 1, 'Broadcast when no charging point exists', NOW(), NOW()),

    ('charge.start.can_not_to_point', 'zh', '充电点不可达，请检查路径是否被阻挡', 1, '充电点不可达时播报', NOW(), NOW()),
    ('charge.start.can_not_to_point', 'en', 'The charging point is unreachable. Please check whether the path is blocked.', 1, 'Broadcast when charging point is unreachable', NOW(), NOW()),

    ('docking.start', 'zh', '到达充电点，开始对接', 1, '到达充电点，开始对接', NOW(), NOW()),
    ('docking.start', 'en', 'Arrived at charging point, starting docking', 1, 'Arrived at charging point, starting docking', NOW(), NOW()),

    ('docking.end', 'zh', '对接成功，正在充电请谨慎操作', 1, '对接成功，正在充电请谨慎操作', NOW(), NOW()),
    ('docking.end', 'en', 'Docking successful, charging in progress. Please operate with caution.', 1, 'Docking successful, charging in progress. Please operate with caution.', NOW(), NOW())
ON DUPLICATE KEY UPDATE
    content = VALUES(content),
    enabled = VALUES(enabled),
    description = VALUES(description),
    updated_at = NOW();