import { useEffect } from 'react';
import styles from './index.module.less'
import Earth from './Earth';

const cityList = {
  上海: { name: "上海", longitude: 121.0, latitude: 31.0 },
  成都: { name: "成都", longitude: 103.0, latitude: 31.0 },
  广州: { name: "广州", longitude: 113.0, latitude: 23.06 },
  纽约: { name: "纽约", longitude: -74.5, latitude: 40.5 },
  伦敦: { name: "伦敦", longitude: 0.1, latitude: 51.3 },
  开普敦: { name: "开普敦", longitude: 18.25, latitude: -33.5 },
  悉尼: { name: "悉尼", longitude: 151.1, latitude: -33.51 },
  东京: { name: "东京", longitude: 139.69, latitude: 35.69 },
  里约热内卢: { name: "里约热内卢", longitude: -43.11, latitude: -22.54 },
}

const relationList = [{
  from: "广州",
  to: ["上海", "成都", "纽约", "伦敦", "开普敦", "悉尼", "东京", "里约热内卢"],
  color: `rgba(255, 255, 255, 1)`,
}]

export const BaseArea = () => {
  useEffect(() => {
    const earth = new Earth('earth', cityList, relationList, {
      showStats: false,
      showGui: false,
      enterAnimation: true,
      star: {
        show: true,
        autoRotate: true,
      },
      earth: {
        autoRotate: true,
        sprite: true,
        outLine: true,
      },
      city: {
        stroke: true,
        flyLine: true,
        line: true,
        point: true,
      }
    })
    return () => {
      earth.clear()
    }
  }, [])

  return <div id="earth" className={styles.earth} />
}
