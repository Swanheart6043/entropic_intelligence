import * as THREE from 'three'
import TWEEN from 'tween'
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js"
import Stats from 'three/examples/jsm/libs/stats.module.js'
import { initScene } from './scene'
import { initCamera } from './camera'
import { initRenderer } from './renderer'
import { initLight } from './light'
import { initControls } from './control'
import { initStarBg } from './starBg'
import { createEarthMesh } from './earth'
import { waveMeshAnimate } from './city/cityPoint'
import { InitFlyLine } from './city/flyLine'
import type { IEarthConfig, IflyData, ICityList } from './type'
import { GlobalConfig } from './config'
import { outLineAnimation } from './earth/outLine'
import { initEvent } from './event'
import { createChinaMesh } from './china'

class Earth {
  mainContainer?: HTMLElement
  animationFrameId: number | null = null
  width?: number
  height?: number
  scene: THREE.Scene | null = null
  camera: THREE.PerspectiveCamera | null = null
  earthObj: THREE.Object3D | null = null
  earthOutLine: THREE.Object3D | null = null
  waceMeshArr: THREE.Object3D | null = null
  renderer: THREE.WebGLRenderer | null = null
  orbitControl: OrbitControls | null = null
  stats?: Stats
  stars?: THREE.Points
  cityList?: ICityList
  relationList?: IflyData[]
  flyManager?: InitFlyLine

  constructor(containerId: string, cityList: ICityList, relationList: IflyData[], config?: IEarthConfig) {
    GlobalConfig.earthRadius = config?.radius ?? GlobalConfig.earthRadius
    GlobalConfig.default = Object.assign({}, GlobalConfig.default, config || {})
    this.cityList = cityList
    this.relationList = relationList
    this.init(containerId)
  }

  /**
   * 初始化
   * @param 无
   * @returns 无
   */
  init(containerId: string) {
    const element = document.getElementById(containerId)
    // 检查容器是否存在
    if (!element) {
      console.error('没有获取到容器元素')
      return
    }
    this.mainContainer = element
    this.width = this.mainContainer.offsetWidth
    this.height = this.mainContainer.offsetHeight
    // 检查容器宽高
    if (!this.width || !this.height) {
      console.error('this.width this.height 不能为空')
      return
    }
    // 初始化渲染器
    this.renderer = initRenderer(this.width, this.height)
    // 初始化场景
    this.scene = initScene()
    if (!this.renderer) {
      console.error('this.renderer 不能为空')
      return
    }
    // 初始化相机
    this.camera = initCamera(this.width, this.height)
    this.mainContainer.appendChild(this.renderer.domElement)
    if (!this.scene) {
      console.error('this.scene 不能为空')
      return
    }
    // 初始化光源
    initLight(this.scene)
    if (!this.camera) {
      console.error('this.camera 不能为空')
      return
    }
    // 初始化轨道控制
    this.orbitControl = initControls(this.camera, this.renderer)
    if (GlobalConfig.default.showStats) {
      this.stats = new Stats()
      this.mainContainer.appendChild(this.stats.dom)
    }
    this.load()
  }

  /**
   * 加载模型
   * @param 无
   * @returns 无
   */
  load() {
    if (!this.mainContainer || !this.scene || !this.camera || !this.renderer) {
      console.error('容器 场景 渲染器 不能为空')
      return
    }
    if (!GlobalConfig.default.star.show) {
      console.error('GlobalConfig.default.star.show 不能为空')
      return
    }
    this.stars = initStarBg(this.scene)
    if (!this.cityList || !this.relationList) {
      console.error('城市数据 位置数据 不能为空')
      return
    }
    const { earthObj, earthOutLine, waveMeshObj, flyManager } = createEarthMesh(this.cityList, this.relationList)
    this.waceMeshArr = waveMeshObj
    this.earthOutLine = earthOutLine
    this.earthObj = earthObj
    this.flyManager = flyManager
    this.scene.add(earthObj)
    const { chinaObj } = createChinaMesh()
    this.scene.add(chinaObj)
    if (!GlobalConfig.default.enterAnimation) {
      console.error('GlobalConfig.default.enterAnimation 不能为空')
      return
    }
    this.enterAnimate(3000)
    this.animate()
    if (!this.earthObj || !this.orbitControl) {
      console.error('this.earthObj this.orbitControl 不能为空')
      return
    }
    initEvent(this.renderer, this.camera, this.earthObj, this.mainContainer, this.orbitControl)
    window.onresize = () => {
      this.resize()
    }
  }

  /**
   * 入场动画
   * @param 无
   * @returns 无
   */
  enterAnimate(time: number) {
    const tween = new TWEEN.Tween(GlobalConfig.camaPositions[0]).to(GlobalConfig.camaPositions[1], time).easing(TWEEN.Easing.Quadratic.InOut)
    const update = () => this.camera?.position.set(GlobalConfig.camaPositions[0].x, GlobalConfig.camaPositions[0].y, GlobalConfig.camaPositions[0].z)
    tween.onUpdate(update)
    tween.start()
  }

  /**
   * 场景渲染
   * @param 无
   * @returns 无
   */
  animate() {
    if (!this.renderer || !this.scene || !this.camera) {
      console.error('场景 渲染器 不能为空')
      return
    }
    if (GlobalConfig.default.star.show && GlobalConfig.default.star.autoRotate && this.stars) {
      this.stars.rotation.y += 0.0001
    }
    if (GlobalConfig.default.earth.autoRotate && this.earthObj) {
      this.earthObj.rotation.y += 0.001
    }
    if (this.stats && GlobalConfig.default.showStats) {
      this.stats.update()
    }
    this.renderer.clear()
    this.renderer.render(this.scene, this.camera)
    this.animationFrameId = requestAnimationFrame(this.animate.bind(this))
    TWEEN.update()
    if (this.flyManager != null) {
      this.flyManager.animation()
    }
    if (GlobalConfig.default.earth.outLine && this.earthOutLine != null) {
      outLineAnimation(this.earthOutLine)
    }
    waveMeshAnimate(this.waceMeshArr)
  }

  /**
   * 窗口变化
   * @param 无
   * @returns 无
   */
  resize() {
    if (!this.mainContainer || !this.camera || !this.renderer) {
      console.error('parentDom camera renderer为空，无法重新渲染')
      return
    }
    this.width = this.mainContainer.offsetWidth || 0
    this.height = this.mainContainer.offsetHeight || 0
    this.camera.aspect = this.width / this.height
    this.camera.updateProjectionMatrix()
    this.renderer.setSize(this.width, this.height)
  }

  /**
   * 清理资源
   * @param 无
   * @returns 无
   */
  clear() {
    this.scene?.children?.forEach(child => {
      if (child instanceof THREE.Mesh) {
        child.geometry.dispose()
        child.material.dispose()
      }
    })
    this.scene = null
    this.camera = null
    this.renderer?.dispose()
    this.renderer = null
    TWEEN.removeAll()
    this.animationFrameId && cancelAnimationFrame(this.animationFrameId)
    this.animationFrameId = null
    window.removeEventListener('resize', this.resize)
  }
}

export default Earth
