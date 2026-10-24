import { useState } from 'react'
import type { FormProps } from 'antd'
import { Form, Input, Button, message, Card } from 'antd'
import dayjs from 'dayjs'
import { projectInitializeService } from '../../services/projectInitialize'

type FieldType = {
  vkey: string
  sn: string
  time: string
}

export const Initialization = () => {
  const [form] = Form.useForm<FieldType>()
  const [saving, setSaving] = useState(false)
  const [messageApi, contextHolder] = message.useMessage()

  const submit: FormProps<FieldType>['onFinish'] = async (values) => {
    try {
      setSaving(true)

      const config = {
        vkey: values.vkey,
        sn: values.sn,
        time: values.time,
      }
      const result = await projectInitializeService.initialize(config)
      if (!result.success) {
        messageApi.error('初始化失败: ' + result.message)
        return
      }
      messageApi.success('初始化成功!')

    } catch (error) {
      const text = error instanceof Error ? error.message : '未知错误'
      messageApi.error('初始化异常: ' + text)
    } finally {
      setSaving(false)
    }
  }

  return (
    <div className="p-[16px]" style={{ width: "100%", height: "100%", overflow: 'auto' }}>
      <Card title={<span className="text-xl font-bold">新机初始化</span>}>
        <Form<FieldType> 
          name="projectInitialize"
          form={form} 
          initialValues={{ sn: '', vkey: '', time: dayjs().format('YYYY-MM-DD HH:mm:ss') }}
          labelCol={{ span: 3 }} 
          style={{ maxWidth: 500 }} 
          onFinish={submit}
        >
          <Form.Item label="NPC" name="vkey" rules={[{ required: true, message: 'npc不能为空!' }]}>
            <Input placeholder="例如: heyn127@2026" />
          </Form.Item>

          <Form.Item label="SN" name="sn" rules={[{ required: true, message: 'sn不能为空!' }]}>
            <Input placeholder="例如: H1202501001" />
          </Form.Item>

          <Form.Item label="时间" name="time" rules={[{ required: true, message: '时间不能为空!' }]}>
            <Input placeholder="例如: 2026-02-01 08:02:00" disabled />
          </Form.Item>

          <Form.Item className="mt-6" wrapperCol={{offset: 3, span: 16}}>
            <Button type="primary" htmlType="submit" loading={saving}>
              一键初始化
            </Button>
          </Form.Item>
        </Form>  
      </Card>
      {contextHolder}
    </div>
  )
}
