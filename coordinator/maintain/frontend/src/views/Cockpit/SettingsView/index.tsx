import type { FormProps } from 'antd';
import { Button, Card, Form, Input, message } from 'antd';
import { useEffect, useState } from 'react';
import { apiService } from '../../../services/api';

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
};

export const RobotSettings = () => {
  const [form] = Form.useForm<FieldType>()
  const [, setLoading] = useState(false)
  const [saving, setSaving] = useState(false)
  const [messageApi, contextHolder] = message.useMessage()

  // 加载配置
  const loadConfig = async () => {
    try {
      setLoading(true);
      // 这里假设配置文件名为 'robot_config'，你需要根据实际情况调整
      const config = await apiService.getConfig();
      
      // 将配置映射到表单字段
      form.setFieldsValue({
        sn: config.sn || '',
        platformWebsocketIp: config.platform?.websocket?.ip || '',
        platformWebsocketPort: config.platform?.websocket?.port?.toString() || '',
        platformWebsocketPath: config.platform?.websocket?.path || '',
        platformHttpIp: config.platform?.http?.ip || '',
        platformHttpPort: config.platform?.http?.port?.toString() || '',
        platformHttpPath: config.platform?.http?.path || '',
        platformRtspIp: config.platform?.rtsp?.ip || '',
        platformRtspPort: config.platform?.rtsp?.port?.toString() || '',
      });

      message.success('配置加载成功');
    } catch (error) {
      message.error('加载配置失败: ' + (error instanceof Error ? error.message : '未知错误'));
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadConfig();
  }, []);

  const onFinish: FormProps<FieldType>['onFinish'] = async () => {
    try {
      messageApi.info("暂未开放，敬请期待")
      // 写入数据库
      
    } catch (error) {
      message.error('保存配置失败: ' + (error instanceof Error ? error.message : '未知错误'));
    } finally {
      setSaving(false);
    }
  }

  return (
    <>
      <Card title={<span className="text-xl font-bold">机器人设置</span>}>
        <Form
          form={form}
          name="configuration"
          layout="vertical"
          onFinish={onFinish}
          autoComplete="off"
        >
          <div className="mb-4 border-b">
            <h3 className="text-base font-semibold mb-4">阈值设置</h3>
          </div>
          <Form.Item<FieldType> label="低电量阈值" name="sn" rules={[{ required: true, message: '低电量阈值不能为空!' }]}>
            <Input placeholder="请输入低电量阈值" />
          </Form.Item>
          <Form.Item<FieldType> label="充满阈值" name="sn" rules={[{ required: true, message: '充满阈值不能为空!' }]}>
            <Input placeholder="请输入充满阈值" />
          </Form.Item>

          <Form.Item className="mt-6">
            <Button type="primary" htmlType="submit" loading={saving}>
              保存
            </Button>
          </Form.Item>
        </Form>
      </Card>

      {contextHolder}
    </>
  )
}