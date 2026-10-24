import { useState } from "react"

const glassPanel = {
  backdropFilter: "blur(12px)",
  WebkitBackdropFilter: "blur(12px)",
  border: "1px solid rgba(0,0,0,0.1)",
  borderRadius: "16px",
  boxShadow: "0 8px 32px 0 rgba(0,0,0,0.3)",
}

const CONTROLS = [
  { icon: "fa-play", label: "切手动", active: false },
  { icon: "fa-play", label: "切自动", active: false },
  { icon: "fa-play", label: "重新巡逻", active: false },
  { icon: "fa-play", label: "取消充电", active: false },
]

export const Control = () => {
  const [activeControl, setActiveControl] = useState(0)

  function RobotStatus() {
    return (
      <div style={{ textAlign: "center" }}>
        <div style={{
          width: 140, height: 140, borderRadius: "50%", border: "2px solid rgba(0, 0, 0, 0.1)",
          margin: "0 auto", position: "relative", display: "flex", alignItems: "center", justifyContent: "center",
          boxShadow: "inset 0 0 20px rgba(0,242,254,0.2)",
        }}>
          <div style={{
            position: "absolute", top: -2, left: -2, right: -2, bottom: -2,
            borderRadius: "50%", border: "2px solid transparent",
            borderTopColor: "#00f2fe", borderRightColor: "#00f2fe",
            transform: "rotate(-45deg)",
          }} />
          <i className="fa-solid fa-robot" style={{ fontSize: 50, color: "#000" }} />
        </div>
        <div style={{ marginTop: 20 }}>
          <div style={{ fontSize: 32, fontWeight: 200, letterSpacing: 1, color: "#000" }}>77%</div>
          <div style={{ fontSize: 12, color: "rgba(0, 0, 0, 0.6)", textTransform: "uppercase", marginTop: 5, letterSpacing: 2 }}>
            Power Level
          </div>
        </div>
      </div>
    )
  }

  function ControlButton({ label, active = false, onClick }: { label: any, active: any, onClick: any }) {
    const [hovered, setHovered] = useState(false)

    const baseStyle = {
      background: active ? "linear-gradient(135deg,#4facfe 0%,#00f2fe 100%)" : hovered ? "rgba(255,255,255,0.15)" : "rgba(255,255,255,0.08)",
      border: active ? "none" : `1px solid ${hovered ? "rgba(255,255,255,0.3)" : "rgba(0,0,0,0.1)"}`,
      color: active ? "#fff" : "#000",
      fontWeight: active ? 600 : 400,
      padding: 15, borderRadius: 12, cursor: "pointer",
      display: "flex", alignItems: "center", gap: 10, fontSize: 14,
      transition: "0.3s",
      transform: hovered && !active ? "translateY(-2px)" : "none",
      boxShadow: hovered && !active ? "0 5px 15px rgba(0,0,0,0.2)" : "none",
    }

    return (
      <button style={baseStyle} onMouseEnter={() => setHovered(true)} onMouseLeave={() => setHovered(false)} onClick={onClick}>
        {label}
      </button>
    )
  }

  return (
    <aside style={{ ...glassPanel, padding: 24, display: "flex", flexDirection: "column", gap: 30 }}>
      <RobotStatus />
      
      <div style={{ display: "flex", flexDirection: "column", gap: 10 }}>
        <div style={{ fontSize: 12, color: "rgba(0, 0, 0, 0.6)", textTransform: "uppercase", letterSpacing: 2, textAlign: "left", marginBottom: 5 }}>
          Controls
        </div>

        {CONTROLS.map((ctrl, i) => (
          <ControlButton
            key={i}
            label={ctrl.label}
            active={i === activeControl}
            onClick={() => setActiveControl(i)}
          />
        ))}
      </div>
    </aside>
  )
}