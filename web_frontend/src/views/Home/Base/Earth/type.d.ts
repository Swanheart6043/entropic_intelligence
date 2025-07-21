
export interface IEarthConfig {
  showStats: boolean
  // 开启调试框
  showGui: boolean
  // 地球半径
  radius?: number
  // 是否开启入场动画
  enterAnimation?: boolean
  // 星星
  star: {
    // 是否自动旋转
    autoRotate?: boolean
    // 是否开启星空背景
    show?: boolean
  }
  // 地球
  earth: {
    // 是否自动旋转
    autoRotate?: boolean
    // 是否显示地球光晕
    sprite?: boolean
    // 是否轮廓高亮
    outLine?: boolean
  }
  // 城市
  city: {
    // 是否省份高亮
    stroke?: boolean
    // 城市飞线
    flyLine?: boolean
    // 城市连线
    line?: boolean
    // 标识点
    point?: boolean
    // 光柱
    light?: boolean
  }
}

export interface ICity {
  name: string,
  longitude: number,
  latitude: number
}

export interface IflyData {
  from: string,
  to: string[],
  color: string,
}

export type ICityList = Record<string, ICity>

export interface IAddFly {
  color: string,
  curve: Vector3[],
  width: number,
  length: number,
  speed: number,
  repeat: number,
  texture?: Texture,
  callback?: Function
}
