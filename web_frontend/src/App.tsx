import { useEffect } from 'react';
import { Routes, Route, useNavigate } from 'react-router-dom';
import logo from '/logo.png'
import { AppLeftMenu } from './AppLeftMenu';
import './App.css'
import { Avatar } from 'antd';
import defaultAvatar from './assets/default-avatar.png'
import { Home } from './views/Home/index';
import { Control } from './views/Control';
import { Environmental } from './views/Environmental';
import { Monitor } from './views/Monitor';
import { PhotoWall } from './views/PhotoWall';

function App() {
  const nav = useNavigate()

  useEffect(() => { 
    nav('/home')
  }, [])

  return (
    <div className='w-full h-full flex' style={{ height: '100%' }}>
      <div className='bg-[#ffffff]' style={{ height: '56px', borderBottom: '1px solid rgba(5, 5, 5, 0.06)', padding: '0px 16px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <img width={28} src={logo} />
          <span style={{ fontSize: '18px', fontWeight: '600' }}>暖宅</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Avatar size="default" src={defaultAvatar} />
          <span style={{ color: 'rgba(0, 0, 0, 0.45)', fontSize: '14px' }}>Raphael_Hu</span>
        </div>
      </div>

      <div style={{ height: 'calc(100% - 57px)', background: '#f5f5f5', display: 'flex' }}>
        <AppLeftMenu />

        <div style={{ flex: '1', overflow: 'auto' }}>
          <Routes>
            <Route path="/home" element={<Home />} />
            <Route path="/environmental" element={<Environmental />} />
            <Route path="/monitor" element={<Monitor />} />
            <Route path="/photo" element={<PhotoWall />} />
            <Route path="/control" element={<Control />} />
            <Route path="*" element={<h2>页面不存在</h2>} />
          </Routes>
        </div>
      </div>
    </div>
  )
}

export default App
