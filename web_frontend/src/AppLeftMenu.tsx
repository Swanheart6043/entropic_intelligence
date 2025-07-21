import { useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Menu } from 'antd';
import type { MenuProps } from 'antd';
import { HomeOutlined, FileImageOutlined, WindowsOutlined, DotChartOutlined, CameraOutlined } from '@ant-design/icons';

export const AppLeftMenu = () => {
  const nav = useNavigate()

  useEffect(() => { 
    nav('/home')
  }, [])
  
  const handleMenuClick: MenuProps['onClick'] = (e) => {
    nav('/' + e.key)
  }

  const items: Required<MenuProps>['items'][number][] = [
     {
      key: 'home',
      label: '首页',
      icon: <HomeOutlined />,
    },
    {
      key: 'control',
      label: '硬件控制',
      icon: <WindowsOutlined />,
    },
    {
      key: 'environmental',
      label: '环境信息',
      icon: <DotChartOutlined />,
    },
    {
      key: 'monitor',
      label: '实时监控',
      icon: <CameraOutlined />,
    },
    {
      key: 'photo',
      label: '历时照片',
      icon: <FileImageOutlined />,
    },
  ]

  return (
    <div style={{ borderRight: '1px solid rgba(5, 5, 5, 0.06)', padding: '0px 8px' }}>
      <Menu
        style={{ width: 256, background: '#f5f5f5', border: 'none' }}
        defaultSelectedKeys={['home']}
        mode="inline"
        items={items}
        onClick={handleMenuClick}
      />
    </div>
  )
}