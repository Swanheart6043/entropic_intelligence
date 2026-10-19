import { useState, useEffect } from 'react';
import { Form, Input, Button, message, Card } from 'antd';
import type { FormProps } from 'antd';
import { apiService } from '../../services/api';

type FieldType = {
  sn: string;
  platformWebsocketIp: string;
  platformWebsocketPort: string;
  platformWebsocketPath: string;
  platformHttpIp: string;
  platformHttpPort: string;
  platformHttpPath: string;
  platformRtspIp: string;
  platformRtspPort: string;
}

export const BasicConfig = () => {
  const [form] = Form.useForm<FieldType>()
  const [saving, setSaving] = useState(false)
  const [messageApi, contextHolder] = message.useMessage()

  useEffect(() => {
    const loadConfig = async () => {
      try {
        const resp = await apiService.getConfig()
        const config = resp.data ?? resp
        form.setFieldsValue({
          sn: config.device?.sn || '',
          platformWebsocketIp: config.platform?.wss_server_ip   || '',
          platformWebsocketPort: config.platform?.wss_server_port?.toString() || '',
          platformWebsocketPath: config.platform?.wss_server_path || '',
          platformHttpIp: config.platform?.http_server_ip   || '',
          platformHttpPort: config.platform?.http_server_port?.toString() || '',
          platformHttpPath: config.platform?.http_server_path || '',
          platformRtspIp: config.platform?.rtsp_server_ip   || '',
          platformRtspPort: config.platform?.rtsp_server_port?.toString() || '',
        })
      } catch (error) {
        messageApi.error('加载配置失败')
      }
    }
    loadConfig()
  }, [])

  const submit: FormProps<FieldType>['onFinish'] = async (values) => {
    const config = {
      device: {
        sn: values.sn,
      },
      platform: {
        wss_server_ip: values.platformWebsocketIp,
        wss_server_port: values.platformWebsocketPort,   // 注意：返回值是字符串 "2025"，看后端要不要 number
        wss_server_path: values.platformWebsocketPath,
        http_server_ip: values.platformHttpIp,
        http_server_port: values.platformHttpPort,
        http_server_path: values.platformHttpPath,
        rtsp_server_ip: values.platformRtspIp,
        rtsp_server_port: values.platformRtspPort,
      },
    }
    try {
      setSaving(true)
      const result = await apiService.updateConfig(config)
      if (!result.success) {
        messageApi.error(result.message || '保存失败')
        return
      }
      messageApi.success('配置保存成功')
    } catch (error) {
      messageApi.error('保存配置失败')
    } finally {
      setSaving(false)
    }
  }

  const onFinishFailed: FormProps<FieldType>['onFinishFailed'] = (errorInfo) => {
    console.log('Failed:', errorInfo);
    messageApi.error('请检查表单填写是否正确')
  }

  return (
    <div className="p-[16px]" style={{ width: "100%", height: "100%", overflow: 'auto' }}>
      <Card title={<span className="text-xl font-bold">机器人配置</span>}>
        <Form
          form={form}
          name="configuration"
          labelCol={{ span: 4 }}
          wrapperCol={{ span: 16 }}
          style={{ maxWidth: 800 }}
          onFinish={submit}
          onFinishFailed={onFinishFailed}
          autoComplete="off"
        >
          <div className="mb-4 pb-2 border-b">
            <h3 className="text-base font-semibold mb-4">基础信息</h3>
          </div>
          <Form.Item<FieldType>
            label="设备SN"
            name="sn"
            rules={[{ required: true, message: '设备SN不能为空!' }]}
            tooltip="机器人唯一标识"
          >
            <Input placeholder="请输入设备SN" />
          </Form.Item>

          <div className="mb-4 pb-2 border-b mt-6">
            <h3 className="text-base font-semibold mb-4">平台HTTP配置</h3>
          </div>
          <Form.Item<FieldType>
            label="HTTP IP"
            name="platformHttpIp"
            rules={[
              { required: true, message: 'HTTP IP不能为空!' },
              { pattern: /^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/, message: '请输入有效的IP地址!' }
            ]}
          >
            <Input placeholder="例如: 192.168.1.100" />
          </Form.Item>
          <Form.Item<FieldType>
            label="HTTP端口"
            name="platformHttpPort"
            rules={[
              { required: true, message: 'HTTP端口不能为空!' },
              { pattern: /^\d+$/, message: '请输入有效的端口号!' }
            ]}
          >
            <Input placeholder="例如: 8000" />
          </Form.Item>
          <Form.Item<FieldType>
            label="HTTP路径"
            name="platformHttpPath"
            rules={[{ required: true, message: 'HTTP路径不能为空!' }]}
          >
            <Input placeholder="例如: /api" />
          </Form.Item>

          <Form.Item className="mt-6">
            <Button type="primary" htmlType="submit" loading={saving}>
              保存配置
            </Button>
          </Form.Item>
        </Form>
      </Card>
      {contextHolder}
    </div>
  )
}