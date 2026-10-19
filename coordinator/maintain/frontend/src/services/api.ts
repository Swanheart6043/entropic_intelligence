import type { ProcessStatus, LogEntry, SystemStats, ApiResponse, RobotConfig } from '../types';

const API_BASE = import.meta.env.VITE_API_BASE || '';

class ApiService {
  // 获取系统统计
  async getSystemStats(): Promise<SystemStats> {
    const response = await fetch(`${API_BASE}/api/system/stats`);
    const data: ApiResponse<SystemStats> = await response.json();
    return data.data!;
  }

  // 获取所有进程状态
  async getProcesses(): Promise<ProcessStatus[]> {
    const response = await fetch(`${API_BASE}/api/processes`);
    const data: ApiResponse<ProcessStatus[]> = await response.json();
    return data.data || [];
  }

  // 获取单个进程状态
  async getProcessStatus(name: string): Promise<ProcessStatus> {
    const response = await fetch(`${API_BASE}/api/processes/${name}`);
    const data: ApiResponse<ProcessStatus> = await response.json();
    return data.data!;
  }

  // 启动进程
  async startProcess(name: string): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/processes/${name}/start`, {
      method: 'POST',
    });
    return await response.json();
  }

  // 停止进程
  async stopProcess(name: string, force: boolean = false): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/processes/${name}/stop?force=${force}`, {
      method: 'POST',
    });
    return await response.json();
  }

  // 重启进程
  async restartProcess(name: string): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/processes/${name}/restart`, {
      method: 'POST',
    });
    return await response.json();
  }

  // 重启所有进程
  async restartAllProcesses(): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/processes/restart-all`, {
      method: 'POST',
    });
    return await response.json();
  }

  // 获取日志
  async getLogs(process?: string, level?: string, limit: number = 100): Promise<LogEntry[]> {
    const params = new URLSearchParams();
    if (process) params.append('process', process);
    if (level) params.append('level', level);
    params.append('limit', limit.toString());

    const response = await fetch(`${API_BASE}/api/logs?${params}`);
    const data: ApiResponse<LogEntry[]> = await response.json();
    return data.data || [];
  }

  // 创建WebSocket连接（实时日志）
  createLogWebSocket(): WebSocket {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = API_BASE ? new URL(API_BASE).host : window.location.host;
    return new WebSocket(`${protocol}//${host}/ws/logs`);
  }

  // 读取配置
  async getConfig(): Promise<any> {
    const response = await fetch(`${API_BASE}/api/configs`);
    const data: ApiResponse<any> = await response.json();
    return data.data;
  }

  // 更新配置
  async updateConfig(config: RobotConfig): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/configs`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ config }),
    });
    return await response.json();
  }

  // 继电器配置页面
  async getTempCtrlStatus(): Promise<any> {
    const response = await fetch(`${API_BASE}/api/temp-controller/status`);
    const data: ApiResponse<any> = await response.json();
    return data;
  }
  async getTempCtrlParams(): Promise<any> {
    const response = await fetch(`${API_BASE}/api/temp-controller/params`);
    const data: ApiResponse<any> = await response.json();
    return data;
  }
  async updateTempCtrlParams(config: any): Promise<any> {
    const response = await fetch(`${API_BASE}/api/temp-controller/params`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config),
    });
    const data: ApiResponse<any> = await response.json();
    return data;
  }
  async forceTempCtrlRelay(config: any): Promise<any> {
    const response = await fetch(`${API_BASE}/api/temp-controller/relay-force`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config),
    });
    const data: ApiResponse<any> = await response.json();
    return data;
  }
}

export const apiService = new ApiService();