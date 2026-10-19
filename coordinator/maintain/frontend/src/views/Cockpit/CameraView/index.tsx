const glassPanel = {
  background: "rgba(255,255,255,0.05)",
  backdropFilter: "blur(12px)",
  WebkitBackdropFilter: "blur(12px)",
  border: "1px solid rgba(255,255,255,0.1)",
  borderRadius: "16px",
  boxShadow: "0 8px 32px 0 rgba(0,0,0,0.3)",
}

export const CameraView = () => {
  function CornerDeco({ position }: { position: any }) {
    const posMap: any = {
      tl: { top: 20, left: 20, borderRight: 0, borderBottom: 0 },
      tr: { top: 20, right: 20, borderLeft: 0, borderBottom: 0 },
      bl: { bottom: 20, left: 20, borderRight: 0, borderTop: 0 },
      br: { bottom: 20, right: 20, borderLeft: 0, borderTop: 0 },
    }
  
    return (<div style={{ 
      position: "absolute", 
      width: 20, 
      height: 20, 
      border: "2px solid #00f2fe", 
      opacity: 0.7, 
      ...posMap[position] }} 
    />)
  }

  return (
    <section style={{ ...glassPanel, position: "relative", overflow: "hidden", padding: 0, display: "flex", flexDirection: "column" }}>
      <div style={{ flex: 1, background: "#000", position: "relative", display: "flex", alignItems: "center", justifyContent: "center" }}>
        {["tl", "tr", "bl", "br"].map((pos) => <CornerDeco key={pos} position={pos} />)}

        <div style={{ textAlign: "center", color: "rgba(255,255,255,0.6)" }}>
          <i className="fa-solid fa-crosshairs" style={{ fontSize: 40, marginBottom: 10, opacity: 0.5, display: "block" }} />
          <p style={{ letterSpacing: 2, fontSize: 12 }}>SYSTEM TRACKING</p>
        </div>
      </div>
    </section>
  )
}