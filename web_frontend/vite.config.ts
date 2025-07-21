import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  server: {
    proxy: {
      '/api': {
        target: 'http://192.168.1.100:8081',
        changeOrigin: true,
      },
    }
  },
  plugins: [
    react(),
    tailwindcss()
  ],
})
