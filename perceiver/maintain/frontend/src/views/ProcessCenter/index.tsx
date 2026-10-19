import { useEffect, useState } from 'react';
import { Table, Button, Space, Tag } from 'antd';
import { message, Popconfirm } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import { ReloadOutlined, PlayCircleOutlined, StopOutlined, SyncOutlined } from '@ant-design/icons';
import { useProcesses } from '../../hooks';
import { apiService } from '../../services/api';
import type { ProcessStatus } from '../../types';

export const Service = () => {
  const { processes, loading, refresh } = useProcesses();
  const [actionLoading, setActionLoading] = useState<Record<string, boolean>>({});

  useEffect(() => {
    refresh()
  }, [])

  // 启动进程
  const handleStart = async (name: string) => {
    try {
      setActionLoading({ ...actionLoading, [name]: true });
      const result = await apiService.startProcess(name);
      if (result.success) {
        message.success(`${name} 启动成功`);
        refresh();
      } else {
        message.error(result.message || '启动失败');
      }
    } catch (error) {
      message.error('启动失败: ' + (error instanceof Error ? error.message : '未知错误'));
    } finally {
      setActionLoading({ ...actionLoading, [name]: false });
    }
  };

  // 停止进程
  const handleStop = async (name: string, force: boolean = false) => {
    try {
      setActionLoading({ ...actionLoading, [name]: true })
      const result = await apiService.stopProcess(name, force)
      if (result.success) {
        message.success(`${name} 已停止`)
        refresh()
      } else {
        message.error(result.message || '停止失败')
      }
    } catch (error) {
      message.error('停止失败: ' + (error instanceof Error ? error.message : '未知错误'))
    } finally {
      setActionLoading({ ...actionLoading, [name]: false })
    }
  }

  // 重启进程
  const handleRestart = async (name: string) => {
    try {
      setActionLoading({ ...actionLoading, [name]: true })
      const result = await apiService.restartProcess(name)
      if (result.success) {
        message.success(`${name} 重启成功`)
        refresh()
      } else {
        message.error(result.message || '重启失败')
      }
    } catch (error) {
      message.error('重启失败: ' + (error instanceof Error ? error.message : '未知错误'))
    } finally {
      setActionLoading({ ...actionLoading, [name]: false })
    }
  }

  const columns: ColumnsType<ProcessStatus> = [
    {
      title: '进程名称',
      dataIndex: 'name',
      key: 'name',
      render: (name: string) => <strong>{name}</strong>,
    },
    {
      title: '状态',
      dataIndex: 'status',
      key: 'status',
      width: 60,
      render: (status: string) => (
        <Tag color={status === 'running' ? 'success' : 'error'} style={{marginRight: 0}}>
          {status === 'running' ? '运行中' : '已停止'}
        </Tag>
      ),
    },
    {
      title: '进程ID',
      dataIndex: 'pid',
      key: 'pid',
      width: 200,
      align: 'right',
      render: (pid: number | null) => pid || '-',
    },
    {
      title: '内存',
      dataIndex: 'memory_mb',
      key: 'memory_mb',
      width: 220,
      align: 'right',
      render: (memory: number) => memory ? `${memory.toFixed(0)} MB` : '-',
    },
    {
      title: '操作',
      key: 'action',
      fixed: 'right',
      width: 200,
      render: (_: any, record: ProcessStatus) => (
        <Space size="small">
          {record.status === 'stopped' ? (
            <Popconfirm title="启动进程" onConfirm={() => handleStart(record.name)}>
              <Button type="primary" size="small" icon={<PlayCircleOutlined />} loading={actionLoading[record.name]}>
                启动
              </Button>
            </Popconfirm>
          ) : (
            <>
              <Popconfirm title="停止进程" onConfirm={() => handleStop(record.name)}>
                <Button danger size="small" icon={<StopOutlined />} loading={actionLoading[record.name]}>
                  停止
                </Button>
              </Popconfirm>
              <Popconfirm title="重启进程" onConfirm={() => handleRestart(record.name)}>
                <Button type="default" size="small" icon={<SyncOutlined />} loading={actionLoading[record.name]}>
                  重启
                </Button>
              </Popconfirm>
            </>
          )}
        </Space>
      ),
    },
  ];

  return (
    <div className="p-[16px]">
      <div className="mb-4 flex justify-between items-center">
        <h2 className="text-xl font-bold">进程管理</h2>
        <Space>
          <Button icon={<ReloadOutlined />} onClick={refresh}>
            刷新
          </Button>
        </Space>
      </div>

      <Table
        dataSource={processes}
        columns={columns}
        rowKey="name"
        loading={loading}
        pagination={false}
        bordered
        size="middle"
      />
    </div>
  );
};
