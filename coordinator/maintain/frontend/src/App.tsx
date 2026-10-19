import { Menu } from 'antd'
import type { MenuProps } from 'antd'
import { Home } from './views/Home'
import { LogCenter } from './views/LogCenter'
import { HistoricalLogList } from './views/LogCenter/HistoricalLogList'
import { BasicConfig } from './views/ConfigCenter/BasicConfig'
import { RelayConfig } from './views/ConfigCenter/RelayConfig'
import { Service } from './views/ProcessCenter'
import { useState } from 'react'
import { RobotUpgrade } from './views/Upgrade'
import { RobotControl } from './views/Cockpit'
import { Initialization } from './views/Initialization'

type MenuItem = Required<MenuProps>['items'][number]

function App() {
  const [selectedMenu, setSelectedMenu] = useState("home")

  const handleMenuChange = (e: any) => {
    setSelectedMenu(e.key)
  }

  const items: MenuItem[] = [
    {
      key: 'initialization', 
      label: '新机设置'
    },
    {
      key: 'home', 
      label: '首页'
    },
    {
      key: 'process', 
      label: '进程管理'
    },
    {
      key: 'logCenter', 
      label: '日志中心',
      children: [
        {key: "logList", label: '日志列表'},
        {key: "realTimeLog", label: '实时日志'},
      ]
    },
    {
      key: 'configCenter', 
      label: '配置中心', 
      children: [
        {key: "basicConfig", label: '基础配置'},
        {key: 'relayConfig', label: '温控配置'},
      ]
    },
    {
      key: 'robotControl',
      label: '驾驶舱'
    },
    {
      key: 'robotUpgrade', 
      label: '机器升级'
    },
  ]

  return (
    <div className='w-full h-full flex flex-col'>
      <div className='h-[56px] flex items-center justify-between px-[16px] ' style={{ backgroundColor: '#001529', transition: 'background-color 0.3s cubic-bezier(0.645, 0.045, 0.355, 1)', backdropFilter: 'blur(8px)', borderBlockEnd: '1px solid rgba(253, 253, 253, 0.12)' }}>
        <div className='flex items-center gap-[8px]'>
          <img className='w-[28px] h-[28px]' rel="icon" src="/react.svg" />
          <span className='text-[16px] text-[#fff] font-[600]'>协调层运维</span>
        </div>
        <div>

        </div>
      </div>

      <div className='flex-1 flex'>
        <div className='bg-[#001529] flex-[0_0_257px] px-[8px]'>
          <Menu
            style={{ height: "100%" }}
            defaultSelectedKeys={['home']}
            mode="inline"
            theme="dark"
            items={items}
            onClick={handleMenuChange}
          />
        </div>
        <div className='flex-1' style={{ height: "calc(100vh - 56px)", overflow: "auto" }}>
          {Boolean(selectedMenu == "initialization") && <Initialization />}
          {Boolean(selectedMenu == "home") && <Home />}
          {Boolean(selectedMenu == "process") && <Service/>}
          {Boolean(selectedMenu == "realTimeLog") && <LogCenter />}
          {Boolean(selectedMenu == "logList") && <HistoricalLogList />}
          {Boolean(selectedMenu == "basicConfig") && <BasicConfig />}
          {Boolean(selectedMenu == "relayConfig") && <RelayConfig />}
          {Boolean(selectedMenu == "robotControl") && <RobotControl/>}
          {Boolean(selectedMenu == "robotUpgrade") && <RobotUpgrade/>}
        </div>
      </div>
    </div>
  )
}

export default App