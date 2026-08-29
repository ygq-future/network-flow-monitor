import React, { createContext, useContext, useState, useMemo, useCallback, useEffect } from 'react'
import { LocaleMode, ResolvedLocale, TranslationSchema } from './locales'
import { enUS } from './en-US'
import { zhCN } from './zh-CN'
import { BridgeContext } from '../../bridge/BridgeContext'
import { LocaleChangedDto, ConfigChangedDto } from '../../bridge/bridgeTypes'
import { defaultBridge } from '../../bridge/nativeBridge'

interface I18nContextValue {
  locale: LocaleMode
  resolvedLocale: ResolvedLocale
  setLocale: (locale: LocaleMode) => void
  t: TranslationSchema
}

const I18nContext = createContext<I18nContextValue | null>(null)

function getSystemLocale(): ResolvedLocale {
  if (typeof navigator !== 'undefined') {
    const lang = navigator.language.toLowerCase()
    if (lang.startsWith('zh')) {
      return 'SimplifiedChinese'
    }
  }
  return 'English'
}

const CATALOGS: Record<ResolvedLocale, TranslationSchema> = {
  English: enUS,
  SimplifiedChinese: zhCN,
}

export interface I18nProviderProps {
  children: React.ReactNode
  initialLocale?: LocaleMode
  onLocaleChange?: (locale: LocaleMode) => void
}

export const I18nProvider: React.FC<I18nProviderProps> = ({ children, initialLocale = 'System', onLocaleChange }) => {
  const [locale, setLocaleState] = useState<LocaleMode>(initialLocale)
  const systemLocale = useMemo(() => getSystemLocale(), [])
  const bridgeContext = useContext(BridgeContext)
  const bridge = bridgeContext?.bridge || defaultBridge

  useEffect(() => {
    const unsubLocale = bridge.on<LocaleChangedDto>('localeChanged', data => {
      if (data && data.locale) {
        setLocaleState(data.locale)
      }
    })
    const unsubConfig = bridge.on<ConfigChangedDto>('configChanged', data => {
      if (data?.config?.appearance?.language) {
        const lang = data.config.appearance.language
        setLocaleState(lang === 'english' ? 'English' : lang === 'simplified_chinese' ? 'SimplifiedChinese' : 'System')
      }
    })
    return () => {
      unsubLocale()
      unsubConfig()
    }
  }, [bridge])

  const resolvedLocale: ResolvedLocale = locale === 'System' ? systemLocale : locale
  const t = CATALOGS[resolvedLocale] || enUS

  const setLocale = useCallback(
    (newLocale: LocaleMode) => {
      setLocaleState(newLocale)
      onLocaleChange?.(newLocale)
    },
    [onLocaleChange],
  )

  const value = useMemo(
    () => ({
      locale,
      resolvedLocale,
      setLocale,
      t,
    }),
    [locale, resolvedLocale, setLocale, t],
  )

  return <I18nContext.Provider value={value}>{children}</I18nContext.Provider>
}

export function useTranslation(): I18nContextValue {
  const context = useContext(I18nContext)
  if (!context) {
    throw new Error('useTranslation must be used within an I18nProvider')
  }
  return context
}
