-- 引导脚本：建库 + 建 schema_migrations 元表
-- 在所有业务 migration 之前执行，仅一次
CREATE DATABASE IF NOT EXISTS patrol_robots_db
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE patrol_robots_db;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version     VARCHAR(32) PRIMARY KEY,
    applied_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;