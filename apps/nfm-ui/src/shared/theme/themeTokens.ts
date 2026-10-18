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
    background: 'rgba(250, 250, 250, 0.72)',
    surface: 'rgba(255, 255, 255, 0.74)',
    surfaceHover: 'rgba(24, 24, 27, 0.04)',
    border: 'rgba(24, 24, 27, 0.08)',
    borderStrong: 'rgba(24, 24, 27, 0.16)',
    textPrimary: '#09090b',
    textSecondary: '#52525b',
    textMuted: '#71717a',
    download: '#18181b',
    downloadBg: 'rgba(24, 24, 27, 0.06)',
    upload: '#52525b',
    uploadBg: 'rgba(24, 24, 27, 0.04)',
    accent: '#18181b',
  },
  Dark: {
    background: 'rgba(12, 12, 14, 0.72)',
    surface: 'rgba(255, 255, 255, 0.045)',
    surfaceHover: 'rgba(255, 255, 255, 0.08)',
    border: 'rgba(255, 255, 255, 0.08)',
    borderStrong: 'rgba(255, 255, 255, 0.16)',
    textPrimary: '#fafafa',
    textSecondary: '#a1a1aa',
    textMuted: '#71717a',
    download: '#fafafa',
    downloadBg: 'rgba(255, 255, 255, 0.08)',
    upload: '#a1a1aa',
    uploadBg: 'rgba(255, 255, 255, 0.04)',
    accent: '#fafafa',
  },
}
