export type ThemeMode = 'System' | 'Light' | 'Dark'
export type ResolvedTheme = 'Light' | 'Dark'

export interface ThemeColors {
  background: string
  surface: string
  surfaceHover: string
  border: string
  borderStrong: string
  textPrimary: string
  textSecondary: string
  textMuted: string
  download: string
  downloadBg: string
  upload: string
  uploadBg: string
  accent: string
}

export const THEME_TOKENS: Record<ResolvedTheme, ThemeColors> = {
  Light: {
    background: '#f8fafc',
    surface: '#ffffff',
    surfaceHover: '#f1f5f9',
    border: '#e2e8f0',
    borderStrong: '#cbd5e1',
    textPrimary: '#0f172a',
    textSecondary: '#475569',
    textMuted: '#94a3b8',
    download: '#0284c7', // Sky-600
    downloadBg: '#e0f2fe',
    upload: '#7c3aed', // Violet-600
    uploadBg: '#ede9fe',
    accent: '#2563eb',
  },
  Dark: {
    background: '#090d16',
    surface: '#131b2e',
    surfaceHover: '#1e293b',
    border: '#1e293b',
    borderStrong: '#334155',
    textPrimary: '#f8fafc',
    textSecondary: '#94a3b8',
    textMuted: '#64748b',
    download: '#38bdf8', // Sky-400
    downloadBg: '#082f49',
    upload: '#a78bfa', // Violet-400
    uploadBg: '#2e1065',
    accent: '#3b82f6',
  },
}
