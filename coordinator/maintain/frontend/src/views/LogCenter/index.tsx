import { useState, useEffect, useRef, useMemo, useCallback } from 'react';
import { Select, Button, Space, Badge, Input } from 'antd';
import { ClearOutlined, CloudSyncOutlined, SearchOutlined } from '@ant-design/icons';
import { useProcesses } from '../../hooks';
import type { LogEntry } from '../../types';
import { apiService } from '../../services/api';

const { Search } = Input;

// WebSocket日志Hook
const useLogWebSocket = () => {
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [connected, setConnected] = useState(false)
  const wsRef = useRef<WebSocket | null>(null)
  const reconnectTimeoutRef = useRef<any>(null)

  const connect = useCallback(() => {
    try {
      const ws = apiService.createLogWebSocket();
      wsRef.current = ws;

      ws.onopen = () => {
        console.log('WebSocket已连接');
        setConnected(true);
        // 发送心跳
        const heartbeat = setInterval(() => {
          if (ws.readyState === WebSocket.OPEN) {
            ws.send('ping');
          }
        }, 30000);
        ws.addEventListener('close', () => clearInterval(heartbeat));
      };

      ws.onmessage = (event) => {
        try {
          const log: LogEntry = JSON.parse(event.data);
          setLogs((prev) => {
            const newLogs = [...prev, log];
            // 限制最多500条
            return newLogs.slice(-500);
          });
        } catch (err) {
          console.error('解析日志失败:', err);
        }
      };

      ws.onerror = (err) => {
        console.error('WebSocket错误:', err);
      };

      ws.onclose = () => {
        console.log('WebSocket已断开');
        setConnected(false);
        // 3秒后重连
        reconnectTimeoutRef.current = setTimeout(connect, 3000);
      };
    } catch (err) {
      console.error('WebSocket连接失败:', err);
      reconnectTimeoutRef.current = setTimeout(connect, 3000);
    }
  }, [])

  useEffect(() => {
    connect()

    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    }
  }, [connect])

  const clearLogs = useCallback(() => {
    setLogs([])
  }, [])

  return { logs, connected, clearLogs }
}

export const LogCenter = () => {
  const { logs, connected, clearLogs } = useLogWebSocket();
  const { processes, refresh } = useProcesses();
  const [filterProcess, setFilterProcess] = useState<string>('');
  const [filterLevel, setFilterLevel] = useState<string>('');
  const [searchText, setSearchText] = useState<string>('');
  const logViewerRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    refresh()
  }, [])

  const filteredLogs = useMemo(() => {
    return logs.filter((log) => {
      if (filterProcess && log.process !== filterProcess) return false
      if (filterLevel && log.level !== filterLevel) return false
      if (searchText && !log.message.toLowerCase().includes(searchText.toLowerCase())) {
        return false
      }
      return true
    })
  }, [logs, filterProcess, filterLevel, searchText])

  // 获取日志级别颜色
  const getLevelColor = (level: string) => {
    const colors: Record<string, string> = {
      debug: '#608b4e',
      info: '#569cd6',
      warn: '#ce9178',
      error: '#f48771',
      critical: '#f44336',
    };
    return colors[level] || '#d4d4d4';
  };

  // 格式化时间
  const formatTime = (timestamp: string) => {
    try {
      const date = new Date(timestamp);
      return date.toLocaleTimeString('zh-CN', { hour12: false });
    } catch {
      return timestamp;
    }
  };

  // 获取进程选项
  const processOptions = useMemo(() => {
    const uniqueProcesses = Array.from(new Set(processes.map(p => p.name)));
    return uniqueProcesses.map(name => ({ label: name, value: name }));
  }, [processes]);

  return (
    <div className="p-[16px] h-full flex flex-col">
      {/* 标题和连接状态 */}
      <div className="mb-4 flex justify-between items-center">
        <Space>
          <h2 className="text-xl font-bold m-0">实时日志</h2>
          <Badge
            status={connected ? 'success' : 'error'}
            text={
              <span>
                <CloudSyncOutlined className="mr-1" />
                {connected ? '已连接' : '未连接'}
              </span>
            }
          />
        </Space>
        <div className="text-gray-500 text-sm">
          共 {filteredLogs.length} 条 / {logs.length} 条
        </div>
      </div>

      {/* 过滤器 */}
      <div className="mb-4">
        <Space size="middle" wrap>
          <Select
            style={{ width: 180 }}
            placeholder="选择进程"
            allowClear
            value={filterProcess || undefined}
            onChange={(value) => setFilterProcess(value || '')}
            options={[{ label: '所有进程', value: '' }, ...processOptions]}
          />
          <Select
            style={{ width: 150 }}
            placeholder="选择级别"
            allowClear
            value={filterLevel || undefined}
            onChange={(value) => setFilterLevel(value || '')}
            options={[
              { label: '所有级别', value: '' },
              { label: 'DEBUG', value: 'debug' },
              { label: 'INFO', value: 'info' },
              { label: 'WARN', value: 'warn' },
              { label: 'ERROR', value: 'error' },
              { label: 'CRITICAL', value: 'critical' },
            ]}
          />
          <Search
            placeholder="搜索日志内容"
            allowClear
            style={{ width: 250 }}
            value={searchText}
            onChange={(e) => setSearchText(e.target.value)}
            prefix={<SearchOutlined />}
          />
          <Button icon={<ClearOutlined />} onClick={clearLogs}>
            清空日志
          </Button>
        </Space>
      </div>

      {/* 日志查看器 */}
      <div
        ref={logViewerRef}
        className="flex-1 bg-[#1e1e1e] p-4 overflow-y-auto font-mono text-[13px] leading-relaxed"
        style={{ minHeight: 500, maxHeight: 'calc(100vh - 190px)' }}
      >
        {filteredLogs.length === 0 ? (
          <div className="text-gray-500 text-center py-8">
            {logs.length === 0 ? '等待日志流入...' : '没有匹配的日志'}
          </div>
        ) : (
          filteredLogs.map((log, index) => (
            <div
              key={index}
              className="py-1 border-b border-gray-800 hover:bg-gray-900"
              style={{ fontSize: '13px' }}
            >
              <span style={{ color: '#608b4e' }}>[{formatTime(log.timestamp || log.received_at)}]</span>
              {' '}
              <span style={{ color: '#4ec9b0' }}>[{log.process}]</span>
              {' '}
              <span
                style={{
                  color: getLevelColor(log.level),
                  fontWeight: 'bold',
                }}
              >
                [{log.level.toUpperCase()}]
              </span>
              {' '}
              <span style={{ color: '#d4d4d4' }}>{log.message}</span>
            </div>
          ))
        )}
      </div>

      {/* 底部提示 */}
      {!connected && (
        <div className="mt-2 text-orange-500 text-sm">
          ⚠️ WebSocket未连接，正在尝试重连...
        </div>
      )}
    </div>
  );
};
