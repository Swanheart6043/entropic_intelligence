// 进程状态类型
export interface ProcessStatus {
  name: string;
  status: 'running' | 'stopped';
  pid: number | null;
  cpu_percent: number;
  memory_mb: number;
  create_time?: string;
  num_threads?: number;
}

// 日志条目类型
export interface LogEntry {
  timestamp: string;
  process: string;
  thread: number;
  level: 'debug' | 'info' | 'warn' | 'error' | 'critical';
  message: string;
  received_at: string;
}

// 系统统计类型
interface Disk {
  device: string; 
  mountpoint: string; 
  fstype: string;
  total_gb: number;
  used_gb: number;
  percent: number;
}
export interface SystemStats {
  cpu_percent: number;
  memory: {
    total_gb: number;
    used_gb: number;
    percent: number;
  };
  disks: Disk[];
  processes: {
    total: number;
    running: number;
    stopped: number;
  };
}

// 配置类型
export interface RobotConfig {
  device: {
    sn: string
    type: string
    language: string
  }
  platform: {
    websocket_url: string
    http_base_url: string
    rtsp_base_url: string
    tls_verify: boolean
    ca_cert_path: string
  }
}

// API响应类型
export interface ApiResponse<T> {
  success: boolean;
  data?: T;
  message?: string;
}
