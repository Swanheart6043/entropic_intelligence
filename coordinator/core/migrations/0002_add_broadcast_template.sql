USE patrol_robots_db;

CREATE TABLE IF NOT EXISTS broadcast_template (
    id INT AUTO_INCREMENT PRIMARY KEY,
    event_key VARCHAR(64) NOT NULL UNIQUE,
    content VARCHAR(500) NOT NULL,
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    description VARCHAR(200) DEFAULT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_event_key (event_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO broadcast_template (event_key, content, description) VALUES
('lidar.someone_on_top',  '注意，请不要靠在我身上，以免发生意外', '激光雷达检测到有人靠在车上');

INSERT IGNORE INTO schema_migrations (version) VALUES ('0002');