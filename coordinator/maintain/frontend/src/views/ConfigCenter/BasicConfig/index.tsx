import { useEffect, useState } from 'react';
import { Button, Card, Checkbox, Form, Input, message } from 'antd';
import type { FormProps } from 'antd';
import { apiService } from '../../../services/api';
import type { RobotConfig } from '../../../types';

type FieldType = {
  sn: string;
  type: string;
  language: string;
  websocketUrl: string;
  httpBaseUrl: string;
  rtspBaseUrl: string;
  tlsVerify: boolean;
  caCertPath: string;
}

type PlatformConfig = Record<string, string | undefined>;

const legacyWebsocketUrl = (platform: PlatformConfig): string => {
  if (!platform.wss_server_ip || !platform.wss_server_port) return '';
  const scheme = platform.websocket_scheme || 'ws';
  const path = platform.wss_server_path || '';
  return `${scheme}://${platform.wss_server_ip}:${platform.wss_server_port}${path}{sn}`;
}

const legacyHttpBaseUrl = (platform: PlatformConfig): string => {
  if (!platform.http_server_ip || !platform.http_server_port) return '';
  const scheme = platform.http_server_scheme || 'http';
  const path = platform.http_server_path || '';
  return `${scheme}://${platform.http_server_ip}:${platform.http_server_port}${path}`;
}

const legacyRtspBaseUrl = (platform: PlatformConfig): string => {
  if (!platform.rtsp_server_ip || !platform.rtsp_server_port) return '';
  return `rtsp://${platform.rtsp_server_ip}:${platform.rtsp_server_port}`;
}

const urlRule = (protocols: string[]) => ({
  validator: async (_: unknown, value?: string) => {
    if (!value) return;
    try {
      const parsed = new URL(value.replaceAll('{sn}', 'DEVICE_SN'));
      if (!protocols.includes(parsed.protocol) || !parsed.hostname) throw new Error();
    } catch {
      throw new Error(`请输入完整 URL（协议为 ${protocols.join(' 或 ')}）`);
    }
  },
});

export const BasicConfig = () => {
  const [form] = Form.useForm<FieldType>();
  const [saving, setSaving] = useState(false);
  const [messageApi, contextHolder] = message.useMessage();

  useEffect(() => {
    const loadConfig = async () => {
      try {
        const config = await apiService.getConfig();
        const platform = (config.platform || {}) as PlatformConfig;
        form.setFieldsValue({
          sn: config.device?.sn || '',
          type: config.device?.type || '',
          language: config.device?.language || 'zh',
          websocketUrl: platform.websocket_url || legacyWebsocketUrl(platform),
          httpBaseUrl: platform.http_base_url || legacyHttpBaseUrl(platform),
          rtspBaseUrl: platform.rtsp_base_url || legacyRtspBaseUrl(platform),
          tlsVerify: platform.tls_verify?.toLowerCase() !== 'false',
          caCertPath: platform.ca_cert_path || '',
        });
      } catch {
        messageApi.error('加载配置失败');
      }
    };
    void loadConfig();
  }, [form, messageApi]);

  const submit: FormProps<FieldType>['onFinish'] = async (values) => {
    const config: RobotConfig = {
      device: {
        sn: values.sn,
        type: values.type,
        language: values.language,
      },
      platform: {
        websocket_url: values.websocketUrl,
        http_base_url: values.httpBaseUrl,
        rtsp_base_url: values.rtspBaseUrl,
        tls_verify: values.tlsVerify,
        ca_cert_path: values.caCertPath || '',
      },
    };
    try {
      setSaving(true);
      const result = await apiService.updateConfig(config);
      if (!result.success) {
        messageApi.error(result.message || '保存失败');
        return;
      }
      messageApi.success('配置保存成功，重启相关服务后生效');
    } catch {
      messageApi.error('保存配置失败');
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="p-[16px]" style={{ width: '100%', height: '100%', overflow: 'auto' }}>
      <Card title={<span className="text-xl font-bold">机器人配置</span>}>
        <Form form={form} name="configuration" labelCol={{ span: 4 }} wrapperCol={{ span: 17 }} style={{ maxWidth: 900 }} onFinish={submit} onFinishFailed={() => messageApi.error('请检查表单填写是否正确')} autoComplete="off">
          <div className="mb-4 pb-2 border-b">
            <h3 className="text-base font-semibold mb-4">基础信息</h3>
          </div>
          <Form.Item<FieldType> label="设备SN" name="sn" rules={[{ required: true, message: 'SN不能为空!' }]}>
            <Input placeholder="请输入设备SN" />
          </Form.Item>
          <Form.Item<FieldType> label="设备型号" name="type" rules={[{ required: true, message: '型号不能为空!' }]}>
            <Input placeholder="例如: H1" />
          </Form.Item>
          <Form.Item<FieldType> label="设备语言" name="language" rules={[{ required: true, message: '语言不能为空!' }]}>
            <Input placeholder="例如: zh" />
          </Form.Item>

          <div className="mb-4 pb-2 border-b mt-6">
            <h3 className="text-base font-semibold mb-4">平台连接配置</h3>
          </div>
          <Form.Item<FieldType>
            label="WebSocket URL"
            name="websocketUrl"
            tooltip="可使用 {sn} 占位符，运行时替换成设备 SN"
            rules={[{ required: true, message: 'WebSocket URL不能为空!' }, urlRule(['ws:', 'wss:'])]}
          >
            <Input placeholder="例如: wss://cloud.example.com/websocket/robot/{sn}" />
          </Form.Item>
          <Form.Item<FieldType>
            label="HTTP Base URL"
            name="httpBaseUrl"
            rules={[{ required: true, message: 'HTTP Base URL不能为空!' }, urlRule(['http:', 'https:'])]}
          >
            <Input placeholder="例如: https://cloud.example.com/robotboot" />
          </Form.Item>
          <Form.Item<FieldType>
            label="RTSP Base URL"
            name="rtspBaseUrl"
            tooltip="系统会在此基址后追加 /live/... 等推流路径"
            rules={[{ required: true, message: 'RTSP Base URL不能为空!' }, urlRule(['rtsp:', 'rtsps:'])]}
          >
            <Input placeholder="例如: rtsp://media.example.com:2554" />
          </Form.Item>

          <div className="mb-4 pb-2 border-b mt-6">
            <h3 className="text-base font-semibold mb-4">TLS配置</h3>
          </div>
          <Form.Item<FieldType> label="校验证书" name="tlsVerify" valuePropName="checked">
            <Checkbox>校验 HTTPS/WSS 服务端证书</Checkbox>
          </Form.Item>
          <Form.Item<FieldType>
            label="CA证书路径"
            name="caCertPath"
            tooltip="使用系统信任链时留空；私有 CA 时填写设备上的证书文件绝对路径"
          >
            <Input placeholder="例如: /etc/patrol_robot_h1/certs/cloud-ca.pem" />
          </Form.Item>

          <Form.Item className="mt-6" wrapperCol={{ offset: 5, span: 17 }}>
            <Button type="primary" htmlType="submit" loading={saving}>
              保存配置
            </Button>
          </Form.Item>
        </Form>
      </Card>
      {contextHolder}
    </div>
  );
}
