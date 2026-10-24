import { Control } from "./ControlView"
import { CameraView } from "./CameraView"
import { RobotSettings } from './SettingsView'

const glassPanel = {
  background: "rgba(255,255,255,0.05)",
  backdropFilter: "blur(12px)",
  WebkitBackdropFilter: "blur(12px)",
  border: "1px solid rgba(255,255,255,0.1)",
  borderRadius: "16px",
  boxShadow: "0 8px 32px 0 rgba(0,0,0,0.3)",
}

export const RobotControl = () => {
  return (
    <div style={{color: "#fff", height: "100%", overflow: "hidden", display: "flex", flexDirection: "column", position: "relative" }}>
      <main style={{ flex: 1, display: "grid", gridTemplateColumns: "280px 1fr 320px", gap: 20, padding: 20 }}>
        <Control />

        <CameraView />

        <aside style={{ ...glassPanel, padding: 20, display: "flex", flexDirection: "column", gap: 30 }}>
          <div style={{ flex: 1, overflowY: "auto" }}>
            <RobotSettings />
          </div>
        </aside>
      </main>
    </div>
  )
}
