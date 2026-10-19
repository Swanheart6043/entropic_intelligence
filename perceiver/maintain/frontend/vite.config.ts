import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
  ],
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
