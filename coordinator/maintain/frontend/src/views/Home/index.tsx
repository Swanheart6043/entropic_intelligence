import { useCallback, useEffect, useState } from "react";
import type { SystemStats } from "../../types";
import { apiService } from "../../services/api";

const useSystemStats = () => {
  const [stats, setStats] = useState<SystemStats | null>(null);
  const [loading, setLoading] = useState(true);

  const fetchStats = useCallback(async () => {
    try {
      const data = await apiService.getSystemStats();
      setStats(data);
    } catch (err) {
      console.error('获取系统统计失败:', err);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchStats();
    const interval = setInterval(fetchStats, 3000); // 每3秒刷新
    return () => clearInterval(interval);
  }, [fetchStats]);

  return { stats, loading, refresh: fetchStats };
};

export const Home = () => {
  const [theme, setTheme] = useState("aurora");
  const { stats } = useSystemStats();

  useEffect(() => {
    if (theme === "zen") {
      document.documentElement.removeAttribute("data-theme");
    } else {
      document.documentElement.setAttribute("data-theme", theme);
    }
  }, [theme]);

  return (
    <div style={{ width: "100%", height: "100%", overflow: 'auto', position: "relative" }}>
      <style>{`
        @import url('https://fonts.googleapis.com/css2?family=Noto+Serif+SC:wght@300;500;700&family=Quicksand:wght@300;400;600&display=swap');

        [data-theme="aurora"] {
            --bg-color: #F8F9FA;
            --card-bg: rgba(255, 255, 255, 0.3);
            --card-border: rgba(255, 255, 255, 0.8);
            --text-main: #4A4A4A;
            --text-sub: #9FA8DA;
            --accent-color: #BA68C8;
            --orb-gradient: linear-gradient(135deg, #81D4FA 0%, #F48FB1 50%, #FFF59D 100%);
            --orb-shadow: rgba(244, 143, 177, 0.4);
            --glow-color-1: #E1BEE7;
            --glow-color-2: #B2EBF2;
        }

        [data-theme="blue"] {
            --bg-color: #F0F4F8;
            --card-bg: rgba(255, 255, 255, 0.6);
            --card-border: rgba(21, 101, 192, 0.1);
            --text-main: #0D47A1;
            --text-sub: #5472d3;
            --accent-color: #1976D2;
            --orb-gradient: linear-gradient(to bottom right, #FFFFFF 10%, #1565C0 100%);
            --orb-shadow: rgba(13, 71, 161, 0.3);
            --glow-color-1: #E3F2FD;
            --glow-color-2: #BBDEFB;
        }

        .ota-root {
            margin: 0; padding: 0; background-color: var(--bg-color); font-family: 'Quicksand', sans-serif;
            color: var(--text-main); height: 100vh; overflow: hidden; display: flex; justify-content: center; align-items: center;
            transition: background-color 0.8s ease, color 0.5s ease;
        }

        .ambient-bg { position: absolute; width: 100%; height: 100%; z-index: -1; overflow: hidden; }
        .orb { position: absolute; border-radius: 50%; filter: blur(80px); opacity: 0.6; transition: background 1s ease; }
        .orb-1 { width: 600px; height: 600px; background: var(--glow-color-1); top: -100px; left: -100px; animation: drift 20s infinite alternate; }
        .orb-2 { width: 500px; height: 500px; background: var(--glow-color-2); bottom: -50px; right: -50px; animation: drift 25s infinite alternate-reverse; }

        @keyframes drift { 0% { transform: translate(0, 0); } 100% { transform: translate(30px, 50px); } }

        .dashboard-container { display: grid; grid-template-columns: 280px 1fr 280px; gap: 40px; width: 85%; max-width: 1400px; align-items: center; }

        .glass-card { background: var(--card-bg); backdrop-filter: blur(25px); -webkit-backdrop-filter: blur(25px); border: 1px solid var(--card-border); border-radius: 24px; padding: 30px; box-shadow: 0 10px 40px -10px var(--orb-shadow); transition: all 0.5s ease; }

        .center-stage { display: flex; flex-direction: column; align-items: center; position: relative; }

        .living-shape { width: 260px; height: 260px; background: var(--orb-gradient); border-radius: 60% 40% 30% 70% / 60% 30% 70% 40%; box-shadow: 0 20px 60px var(--orb-shadow), inset 5px 5px 20px rgba(255,255,255,0.4); animation: morph 8s ease-in-out infinite; display: flex; align-items: center; justify-content: center; color: white; font-family: 'Noto Serif SC'; font-size: 1.4rem; letter-spacing: 2px; z-index: 10; transition: background 1s ease, box-shadow 1s ease; text-shadow: 0 2px 4px rgba(0,0,0,0.1); }

        .ripple { position: absolute; width: 260px; height: 260px; border: 1px solid var(--accent-color); border-radius: 60% 40% 30% 70% / 60% 30% 70% 40%; opacity: 0; animation: ripple-anim 4s linear infinite; }
        .ripple:nth-child(2) { animation-delay: 2s; }

        @keyframes morph { 0%, 100% { border-radius: 60% 40% 30% 70% / 60% 30% 70% 40%; transform: scale(1); } 50% { border-radius: 30% 60% 70% 40% / 50% 60% 30% 60%; transform: scale(1.05); } }
        @keyframes ripple-anim { 0% { transform: scale(1); opacity: 0.5; } 100% { transform: scale(1.6); opacity: 0; } }

        h2 { font-family: 'Noto Serif SC'; font-weight: 500; font-size: 1.3rem; margin: 0 0 20px 0; color: var(--text-main); }
        .data-label { font-size: 0.85rem; color: var(--text-sub); margin-bottom: 5px; }
        .data-value { font-size: 2rem; font-weight: 300; color: var(--text-main); }
        .status-dot { display: inline-block; width: 8px; height: 8px; background-color: var(--accent-color); border-radius: 50%; margin-right: 8px; box-shadow: 0 0 8px var(--accent-color); transition: background-color 0.5s; }

        .log-item { display: flex; justify-content: space-between; padding: 10px 0; border-bottom: 1px solid rgba(0,0,0,0.05); font-size: 0.85rem; color: var(--text-main); }
        .time-stamp { color: var(--text-sub); opacity: 0.7; }

        .theme-switcher { position: absolute; top: 20px; right: 20px; background: rgba(255,255,255,0.5); padding: 5px; border-radius: 30px; backdrop-filter: blur(10px); display: flex; gap: 5px; z-index: 100; }
        .theme-btn { border: none; padding: 8px 15px; border-radius: 20px; cursor: pointer; font-size: 0.8rem; font-family: 'Noto Serif SC'; transition: all 0.3s; opacity: 0.7; }
        .theme-btn:hover, .theme-btn.active { opacity: 1; transform: scale(1.05); box-shadow: 0 2px 5px rgba(0,0,0,0.1); }

        #btn-aurora { background: linear-gradient(135deg, #E1BEE7, #B2EBF2); color: #4A148C; }
        #btn-blue { background: #E3F2FD; color: #0D47A1; }
      `}</style>

      <div className="ota-root" style={{ width: '100%', height: '100%' }}>
        <div className="theme-switcher">
          <button
            id="btn-aurora"
            className={`theme-btn ${theme === "aurora" ? "active" : ""}`}
            onClick={() => setTheme("aurora")}
          >
            极光 (Aurora)
          </button>

          <button
            id="btn-blue"
            className={`theme-btn ${theme === "blue" ? "active" : ""}`}
            onClick={() => setTheme("blue")}
          >
            青花 (Porcelain)
          </button>
        </div>

        <div className="ambient-bg">
          <div className="orb orb-1" />
          <div className="orb orb-2" />
        </div>

        <div className="dashboard-container">
          <div className="glass-card">
            <h2>
              <span className="status-dot" />CPU/内存
            </h2>
            <div style={{ marginBottom: 25 }}>
              <div className="data-label">CPU</div>
              <div className="data-value">{stats?.cpu_percent.toFixed(1) || '0.0'}%</div>
            </div>
            <div style={{ marginBottom: 25 }}>
              <div className="data-label">内存</div>
              <div className="data-value">{stats?.memory.percent.toFixed(1) || '0.0'}%</div>
            </div>
          </div>

          <div className="center-stage">
            <div className="ripple" />
            <div className="ripple" />
            <div className="living-shape">
              <div>H1</div>
            </div>
            <p style={{ marginTop: 30, fontFamily: "Noto Serif SC", color: "var(--text-sub)", opacity: 0.8, letterSpacing: "1px" }}>
              万物互联，生生不息
            </p>
          </div>

          <div className="glass-card">
            <h2>
              <span className="status-dot" />硬盘
            </h2>
            {stats?.disks?.map((item: any) => (
              <div style={{ marginBottom: 25 }}>
                <div className="data-label">{item?.device} ({item?.total_gb}GB)</div>
                <div className="data-value">{item?.percent?.toFixed(1) || '0.0'}%</div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
