import { useState, useEffect, useRef, useCallback } from 'react';
import { Form, InputNumber, Select, Button, message, Card, Tag, Space, Radio, Spin, Statistic } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import type { FormProps } from 'antd';
import { apiService } from '../../../services/api';

type ParamsType = {
  temp_unit: number;
  filter: number;
  compensation: number;
  power_on_delay: number;
  relay1_type: number;
  relay1_setpoint: number;
  relay1_hysteresis: number;
  relay1_on_delay: number;
  relay1_off_delay: number;
  relay2_type: number;
  relay2_setpoint: number;
  relay2_hysteresis: number;
  relay2_on_delay: number;
  relay2_off_delay: number;
};

type StatusType = {
  temperature: number;
  unit: 'C' | 'F';
  relay1_engaged: boolean;
  relay2_engaged: boolean;
};

const ALARM_TYPE_OPTIONS = [
  { value: 0, label: 'H 上限报警 (默认)' },
  { value: 1, label: 'L 下限报警' },
  { value: 2, label: 'HL 上下限报警' },
  { value: 3, label: 'FRO 防冻模式' },
  { value: 4, label: '间歇工作模式' },
];

const UNIT_OPTIONS = [
  { value: 0, label: '摄氏 °C' },
  { value: 1, label: '华氏 °F' },
];

export const RelayConfig = () => {
  const [form] = Form.useForm<ParamsType>();
  const [status, setStatus] = useState<StatusType | null>(null);
  const [statusError, setStatusError] = useState('');
  const [loadingParams, setLoadingParams] = useState(false);
  const [saving, setSaving] = useState(false);
  const [forcing, setForcing] = useState(false);
  const [messageApi, contextHolder] = message.useMessage();
  // 保存加载时的参数快照, 提交时只写改动项 (每寄存器写入约150ms, 少写少等)
  const loadedParams = useRef<ParamsType | null>(null);

  const refreshStatus = useCallback(async () => {
    try {
      const resp = await apiService.getTempCtrlStatus();
      if (resp.success) {
        setStatus(resp.data);
        setStatusError('');
      } else {
        setStatusError(resp.message || '读取失败');
      }
    } catch {
      setStatusError('温控仪状态读取失败');
    }
  }, []);

  const loadParams = useCallback(async () => {
    setLoadingParams(true);
    try {
      const resp = await apiService.getTempCtrlParams();
      console.log(resp, "x+++++++++")
      if (resp.success) {
        form.setFieldsValue(resp.data);
        loadedParams.current = resp.data;
        if (resp.message) messageApi.warning(resp.message);
      } else {
        messageApi.error(resp.message || '加载温控仪参数失败');
      }
    } catch {
      messageApi.error('加载温控仪参数异常');
    } finally {
      setLoadingParams(false);
    }
  }, [form, messageApi]);

  useEffect(() => {
    refreshStatus();
    loadParams();
  }, []);

  const submit: FormProps<ParamsType>['onFinish'] = async (values) => {
    // 只提交与加载快照不同的字段
    const changed: Partial<ParamsType> = {};
    (Object.keys(values) as (keyof ParamsType)[]).forEach((k) => {
      if (loadedParams.current == null || values[k] !== loadedParams.current[k]) {
        changed[k] = values[k] as never;
      }
    });
    if (Object.keys(changed).length === 0) {
      messageApi.info('参数没有改动');
      return;
    }
    try {
      setSaving(true);
      const result = await apiService.updateTempCtrlParams(changed);
      if (!result.success) {
        messageApi.error(result.message || '写入失败');
        return;
      }
      messageApi.success(result.message || '参数写入成功');
      loadedParams.current = { ...(loadedParams.current as ParamsType), ...changed };
      refreshStatus();
    } catch {
      messageApi.error('写入温控仪参数异常');
    } finally {
      setSaving(false);
    }
  };

  const forceRelay = async (relay: number, mode: number) => {
    try {
      setForcing(true);
      const result = await apiService.forceTempCtrlRelay({ relay, mode });
      if (result.success) {
        messageApi.success(result.message);
        refreshStatus();
      } else {
        messageApi.error(result.message || '控制失败');
      }
    } catch {
      messageApi.error('继电器控制失败');
    } finally {
      setForcing(false);
    }
  };

  const unitSuffix = status?.unit === 'F' ? '°F' : '°C';

  return (
    <div className="p-[16px]" style={{ width: '100%', height: '100%', overflow: 'auto', display: "flex", flexDirection: "column", gap: "16px" }}>
      {/* 实时状态 */}
      <div style={{ backgroundColor: "#f0f0f0", padding: "16px" }}>
        <Card
          title={<span className="text-xl font-bold">温控仪状态</span>}
          extra={<Button icon={<ReloadOutlined />} onClick={refreshStatus} size="small">刷新</Button>}
        >
          {statusError ? (
            <div className="text-red-500">{statusError}</div>
          ) : (
            <Space size="large">
              <Statistic
                title="当前温度"
                value={status?.temperature ?? '--'}
                precision={1}
                suffix={unitSuffix}
              />
              <Statistic
                title="继电器1"
                valueRender={() =>
                  status ? (
                    <Tag color={status.relay1_engaged ? 'red' : 'green'}>
                      {status.relay1_engaged ? '吸合' : '释放'}
                    </Tag>
                  ) : '--'
                }
              />
              <Statistic
                title="继电器2"
                valueRender={() =>
                  status ? (
                    <Tag color={status.relay2_engaged ? 'red' : 'green'}>
                      {status.relay2_engaged ? '吸合' : '释放'}
                    </Tag>
                  ) : '--'
                }
              />
            </Space>
          )}
        </Card>
      </div>

      {/* 参数配置 */}
      <div style={{ backgroundColor: "#f0f0f0", padding: "16px" }}>
        <Card
          title={<span className="text-xl font-bold">温控仪参数</span>}
          className="mt-10"
          extra={<Button icon={<ReloadOutlined />} onClick={loadParams} size="small" loading={loadingParams}>读取</Button>}
        >
          <Spin spinning={loadingParams} tip="正在从温控仪逐寄存器读取参数...">
            <Form
              form={form}
              name="tempController"
              labelCol={{ span: 4 }}
              wrapperCol={{ span: 16 }}
              style={{ maxWidth: 800 }}
              onFinish={submit}
              autoComplete="off"
            >
              <div className="mb-4 pb-2 border-b">
                <h3 className="text-base font-semibold mb-4">基础参数</h3>
              </div>
              <Form.Item<ParamsType> label="温度单位" name="temp_unit"
                tooltip="切换单位后, 所有温度量纲参数会自动换算">
                <Select options={UNIT_OPTIONS} />
              </Form.Item>
              <Form.Item<ParamsType> label="滤波系数" name="filter" tooltip="0~10, 出厂默认 3">
                <InputNumber min={0} max={10} precision={0} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="温度补偿" name="compensation" tooltip="-20.0 ~ 20.0">
                <InputNumber min={-20} max={20} step={0.1} precision={1} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="上电延时" name="power_on_delay" tooltip="0~999 秒">
                <InputNumber min={0} max={999} precision={0} style={{ width: 160 }} />
              </Form.Item>

              <div className="mb-4 pb-2 border-b mt-6">
                <h3 className="text-base font-semibold mb-4">继电器1</h3>
              </div>
              <Form.Item<ParamsType> label="报警类型" name="relay1_type">
                <Select options={ALARM_TYPE_OPTIONS} />
              </Form.Item>
              <Form.Item<ParamsType> label="动作值" name="relay1_setpoint">
                <InputNumber min={-58} max={248} step={0.1} precision={1} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="回差值" name="relay1_hysteresis">
                <InputNumber min={0} max={248} step={0.1} precision={1} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="动作延时" name="relay1_on_delay" tooltip="0~999 秒">
                <InputNumber min={0} max={999} precision={0} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="解除延时" name="relay1_off_delay" tooltip="0~999 秒">
                <InputNumber min={0} max={999} precision={0} style={{ width: 160 }} />
              </Form.Item>

              <div className="mb-4 pb-2 border-b mt-6">
                <h3 className="text-base font-semibold mb-4">继电器2</h3>
              </div>
              <Form.Item<ParamsType> label="报警类型" name="relay2_type">
                <Select options={ALARM_TYPE_OPTIONS} />
              </Form.Item>
              <Form.Item<ParamsType> label="动作值" name="relay2_setpoint">
                <InputNumber min={-58} max={248} step={0.1} precision={1} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="回差值" name="relay2_hysteresis">
                <InputNumber min={0} max={248} step={0.1} precision={1} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="动作延时" name="relay2_on_delay" tooltip="0~999 秒">
                <InputNumber min={0} max={999} precision={0} style={{ width: 160 }} />
              </Form.Item>
              <Form.Item<ParamsType> label="解除延时" name="relay2_off_delay" tooltip="0~999 秒">
                <InputNumber min={0} max={999} precision={0} style={{ width: 160 }} />
              </Form.Item>

              <Form.Item className="mt-6">
                <Button type="primary" htmlType="submit" loading={saving}>
                  写入配置
                </Button>
                <span className="ml-3 text-gray-400 text-sm">只写入有改动的项, 逐项读回校验</span>
              </Form.Item>
            </Form>
          </Spin>
        </Card>
      </div>

      {/* 继电器强制控制 */}
      <div style={{ backgroundColor: "#f0f0f0", padding: "16px" }}>
        <Card
          title={<span className="text-xl font-bold">继电器强制控制</span>}
          className="mt-4"
        >
          <div className="text-gray-400 text-sm mb-4">
            强制模式下仪表数码管显示 "---", 属正常现象; 测试完成后务必切回"正常模式",
            否则温控逻辑不再生效
          </div>
          <Spin spinning={forcing}>
            {[1, 2].map((relay) => (
              <div key={relay} className="mb-3">
                <span className="mr-4 font-semibold">继电器{relay}</span>
                <Radio.Group
                  buttonStyle="solid"
                  onChange={(e) => forceRelay(relay, e.target.value)}
                >
                  <Radio.Button value={0}>正常模式</Radio.Button>
                  <Radio.Button value={1}>强制关闭</Radio.Button>
                  <Radio.Button value={2}>强制开启</Radio.Button>
                </Radio.Group>
              </div>
            ))}
          </Spin>
        </Card>
      </div>

      {contextHolder}
    </div>
  );
};