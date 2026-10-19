import type { ProcessStatus, SystemStats, ApiResponse, RobotConfig } from '../types';

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
}

export const apiService = new ApiService();