import { useState, useCallback } from 'react';
import { apiService } from '../services/api';
import type { ProcessStatus } from '../types';

// 进程状态Hook
export const useProcesses = () => {
  const [processes, setProcesses] = useState<ProcessStatus[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const fetchProcesses = useCallback(async () => {
    try {
      setLoading(true);
      const data = await apiService.getProcesses()
      setProcesses(data)
      setError(null)
    } catch (err) {
      setError(err instanceof Error ? err.message : '获取进程状态失败')
    } finally {
      setLoading(false)
    }
  }, [])

  return { processes, loading, error, refresh: fetchProcesses }
}