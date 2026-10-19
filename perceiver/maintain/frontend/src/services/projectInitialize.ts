import type { ApiResponse } from '../types'

const API_BASE = import.meta.env.VITE_API_BASE || ''

class ProjectInitializeService {
  async initialize(config: any): Promise<ApiResponse<any>> {
    const response = await fetch(`${API_BASE}/api/initialize`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config),
    })
    return await response.json()
  }
}

export const projectInitializeService = new ProjectInitializeService()