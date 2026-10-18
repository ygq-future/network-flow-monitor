import React, { createContext, useContext, useEffect, useState, useMemo, useCallback } from 'react'
import { ThemeMode, ResolvedTheme } from './themeTokens'
import { BridgeContext } from '../../bridge/BridgeContext'
import { ThemeChangedDto, ConfigChangedDto } from '../../bridge/bridgeTypes'
import { defaultBridge } from '../../bridge/nativeBridge'

interface ThemeContextValue {
  theme: ThemeMode
  resolvedTheme: ResolvedTheme
  prefersReducedMotion: boolean
  setTheme: (theme: ThemeMode) => void
}

const ThemeContext = createContext<ThemeContextValue | null>(null)

function getSystemTheme(): ResolvedTheme {
  if (typeof window !== 'undefined' && window.matchMedia) {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'Dark' : 'Light'
  }
  return 'Light'
}

function getPrefersReducedMotion(): boolean {
  if (typeof window !== 'undefined' && window.matchMedia) {
    return window.matchMedia('(prefers-reduced-motion: reduce)').matches
  }
  return false
}

export interface ThemeProviderProps {
  children: React.ReactNode
  initialTheme?: ThemeMode
  onThemeChange?: (theme: ThemeMode) => void
}

export const ThemeProvider: React.FC<ThemeProviderProps> = ({ children, initialTheme = 'System', onThemeChange }) => {
  const [theme, setThemeState] = useState<ThemeMode>(initialTheme)
  const [systemTheme, setSystemTheme] = useState<ResolvedTheme>(getSystemTheme)
  const [prefersReducedMotion, setPrefersReducedMotion] = useState<boolean>(getPrefersReducedMotion)
  const bridgeContext = useContext(BridgeContext)
  const bridge = bridgeContext?.bridge || defaultBridge

  useEffect(() => {
    if (typeof window === 'undefined' || !window.matchMedia) return
    const mediaTheme = window.matchMedia('(prefers-color-scheme: dark)')
    setSystemTheme(mediaTheme.matches ? 'Dark' : 'Light')
    const listenerTheme = (e: MediaQueryListEvent) => {
      setSystemTheme(e.matches ? 'Dark' : 'Light')
    }
    mediaTheme.addEventListener('change', listenerTheme)

    const mediaMotion = window.matchMedia('(prefers-reduced-motion: reduce)')
    setPrefersReducedMotion(mediaMotion.matches)
    const listenerMotion = (e: MediaQueryListEvent) => {
      setPrefersReducedMotion(e.matches)
    }
    mediaMotion.addEventListener('change', listenerMotion)

    return () => {
      mediaTheme.removeEventListener('change', listenerTheme)
      mediaMotion.removeEventListener('change', listenerMotion)
    }
  }, [])

  useEffect(() => {
    const unsubTheme = bridge.on<ThemeChangedDto>('themeChanged', data => {
      if (data && data.theme) {
        setThemeState(data.theme)
      }
    })
    const unsubConfig = bridge.on<ConfigChangedDto>('configChanged', data => {
      if (data?.config?.appearance?.theme) {
        const t = data.config.appearance.theme
        setThemeState(t === 'light' ? 'Light' : t === 'dark' ? 'Dark' : 'System')
      }
    })
    return () => {
      unsubTheme()
      unsubConfig()
    }
  }, [bridge])

  const resolvedTheme: ResolvedTheme = theme === 'System' ? systemTheme : theme

  useEffect(() => {
    const root = document.documentElement
    if (resolvedTheme === 'Dark') {
      root.classList.add('dark')
      root.style.colorScheme = 'dark'
    } else {
      root.classList.remove('dark')
      root.style.colorScheme = 'light'
    }
  }, [resolvedTheme])

  const setTheme = useCallback(
    (newTheme: ThemeMode) => {
      setThemeState(newTheme)
      onThemeChange?.(newTheme)
    },
    [onThemeChange],
  )

  const value = useMemo(
    () => ({
      theme,
      resolvedTheme,
      prefersReducedMotion,
      setTheme,
    }),
    [theme, resolvedTheme, prefersReducedMotion, setTheme],
  )

  return <ThemeContext.Provider value={value}>{children}</ThemeContext.Provider>
}

export function useTheme(): ThemeContextValue {
  const context = useContext(ThemeContext)
  if (!context) {
    throw new Error('useTheme must be used within a ThemeProvider')
  }
  return context
}
