import React from 'react'
import styles from './index.module.less'
import { BaseArea } from './Base'
import { useEffect, useState } from 'react'
import moment from 'moment'

export const Home: React.FC = () => {
  const [time, setTime] = useState(moment())

  useEffect(() => {
    const id = setInterval(() => setTime(moment()), 1000)
    return () => {
      clearInterval(id)
    }
  }, [])

  return (
    <div className={styles.home}>
      <div className={styles.head}>
        <div className={styles.headLeft}>
          {moment(time).format('HH:mm:ss')}
        </div>
        
        <h3>暖宅</h3>
        
        <div className={styles.headRight}>
          {moment(time).format('YYYY年MM月DD号')}
        </div>
      </div>

      <div className={styles.body}>
        <BaseArea />
      </div>
    </div>
  )
}
