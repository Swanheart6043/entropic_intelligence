import { useEffect, useState } from "react"
import { fetchEnvironmentalData } from "../apis/api";
import type { CardProps } from "../apis/apiType";

const Card = (props: CardProps) => {
  const { name, count } = props || {}

  return (
    <div style={{ 
      flex: '1',
      backgroundColor: '#fff',
      boxShadow: '0 1px 2px 0 rgba(0, 0, 0, 0.03), 0 1px 6px -1px rgba(0, 0, 0, 0.02), 0 2px 4px 0 rgba(0, 0, 0, 0.02)', 
      padding: '20px 24px'
    }}>
      <span>{name || '-'}：{count || 0}</span>
    </div>
  )
}

export const Environmental = () => {
  const [a9List, setA9List] = useState<CardProps[]>([]);
  const [zeebigList, setZeebigList] = useState<CardProps[]>([]);
  
  useEffect(() => {
    const getData = async () => {
      const result = await fetchEnvironmentalData()
      setA9List(result.data?.a9 || [])
      setZeebigList(result.data?.zeebig || [])
    }

    getData()
    
    const timeId = setInterval(() => {
      getData()
    }, 5000)

    return () => {
      clearInterval(timeId)
    }
  }, [])

  return (
    <div style={{ height: '100%', padding: '20px' }}>
      <div style={{ backgroundColor: '#8982fa', padding: '20px', marginBottom: '20px' }}>
        <div style={{ marginBottom: '10px' }}>
          A9
        </div>
        <div style={{ color: '#000' }}>
          <Card name={a9List[0]?.name} count={a9List[0]?.count} />
          <Card name={a9List[1]?.name} count={a9List[1]?.count} />
          <Card name={a9List[2]?.name} count={a9List[2]?.count} />
          <Card name={a9List[3]?.name} count={a9List[3]?.count} />
          <Card name={a9List[4]?.name} count={a9List[4]?.count} />
          <Card name={a9List[5]?.name} count={a9List[5]?.count} />
          <Card name={a9List[6]?.name} count={a9List[6]?.count} />
        </div>
      </div>

      <div style={{ backgroundColor: '#8982fa', padding: '20px' }}>
        <div style={{ marginBottom: '10px' }}>
          ZeeBig
        </div>
        <div style={{ color: '#000' }}>
          <Card name={zeebigList[0]?.name} count={zeebigList[0]?.count} />
          <Card name={zeebigList[1]?.name} count={zeebigList[1]?.count} />
        </div>
      </div>
    </div>
  )
}