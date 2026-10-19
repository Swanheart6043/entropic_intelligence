import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
  ],
  build: {
    // 运维后端会把 web 目录打进发布包，构建时直接刷新该目录，避免发布旧页面。
    outDir: '../backend/web',
    emptyOutDir: true,
  },
  server: {
    proxy: {
      '/api': {
        target: 'http://10.168.1.110:8000',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://36.134.171.85:31219',
        ws: true,
      },
    },
  }
})
