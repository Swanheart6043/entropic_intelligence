CREATE TABLE IF NOT EXISTS runtime_state (
    id              TINYINT UNSIGNED NOT NULL DEFAULT 1,
    robot_mode      ENUM('idle','patrolling','charging','docking','paused','error') NOT NULL DEFAULT 'idle',
    current_task_id BIGINT NULL,            -- 当前任务,无则 NULL
    waypoint_index  INT NULL,               -- 巡逻到第几个点(粗粒度)
    is_paused       TINYINT(1) NOT NULL DEFAULT 0,
    last_battery_pct TINYINT UNSIGNED NULL,
    updated_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    CONSTRAINT chk_runtime_single_row CHECK (id = 1)
);

-- 幂等 seed: 重跑不报错也不覆盖已有值
INSERT INTO runtime_state (id) VALUES (1) ON DUPLICATE KEY UPDATE id = id;