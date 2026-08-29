import React, { useState } from 'react'
import { ThemeProvider, useTheme } from '../shared/theme/ThemeContext'
import { I18nProvider, useTranslation } from '../shared/i18n/I18nContext'
import { BridgeProvider, useBridge } from '../bridge/BridgeContext'
import { LiveTracer } from '../features/tracer/LiveTracer'
import { SettingsCenter } from '../features/settings/SettingsCenter'
import { DockCapsule } from '../features/capsule/DockCapsule'
import { AppLogo } from '../shared/components/AppLogo'
import { Layers, BarChart3, Settings as SettingsIcon, Minus, Square, X, Sun, Moon, Languages } from 'lucide-react'
import { IconButton } from '../shared/components/IconButton'

type Route = 'capsule' | 'tracer' | 'settings'

const TitleBar: React.FC<{ activeRoute: Route; onNavigate: (r: Route) => void }> = ({ activeRoute, onNavigate }) => {
  const { t, locale, setLocale } = useTranslation()
  const { theme, resolvedTheme, setTheme } = useTheme()
  const bridge = useBridge()

  const toggleTheme = () => {
    const nextTheme = resolvedTheme === 'Dark' ? 'Light' : 'Dark'
    setTheme(nextTheme)
    bridge.sendCommand('setTheme', { theme: nextTheme })
  }

  const toggleLocale = () => {
    const nextLocale = locale === 'SimplifiedChinese' ? 'English' : 'SimplifiedChinese'
    setLocale(nextLocale)
    bridge.sendCommand('setLocale', { locale: nextLocale })
  }

  return (
    <header
      onMouseDown={e => {
        if (
          e.button === 0 &&
          (e.target as HTMLElement).tagName !== 'BUTTON' &&
          !(e.target as HTMLElement).closest('button')
        ) {
          bridge.sendCommand('startWindowDrag')
        }
      }}
      onDoubleClick={e => {
        if (
          e.button === 0 &&
          (e.target as HTMLElement).tagName !== 'BUTTON' &&
          !(e.target as HTMLElement).closest('button')
        ) {
          bridge.sendCommand('toggleMaximize')
        }
      }}
      className="titlebar-drag flex h-10 w-full shrink-0 cursor-move items-center justify-between border-b border-slate-200/80 bg-white/80 px-3 backdrop-blur-md select-none dark:border-slate-800/80 dark:bg-slate-900/80">
      {/* Left: App Identity */}
      <div className="titlebar-drag flex items-center gap-2">
        <AppLogo className="h-5 w-5" />
        <span className="text-xs font-bold text-slate-800 dark:text-slate-200">{t.common.appName}</span>
      </div>

      {/* Center: Navigation Switcher Pills */}
      <nav className="titlebar-no-drag flex items-center rounded-lg bg-slate-100 p-0.5 dark:bg-slate-800/60">
        <button
          onClick={() => onNavigate('capsule')}
          className={`flex cursor-pointer items-center gap-1.5 rounded-md px-2.5 py-1 text-xs font-medium transition-colors ${
            activeRoute === 'capsule'
              ? 'bg-white text-blue-600 shadow-xs dark:bg-slate-700 dark:text-blue-400'
              : 'text-slate-600 hover:text-slate-900 dark:text-slate-400 dark:hover:text-slate-200'
          }`}>
          <Layers className="h-3 w-3" />
          <span>{t.settings.capsule}</span>
        </button>
        <button
          onClick={() => onNavigate('tracer')}
          className={`flex cursor-pointer items-center gap-1.5 rounded-md px-2.5 py-1 text-xs font-medium transition-colors ${
            activeRoute === 'tracer'
              ? 'bg-white text-blue-600 shadow-xs dark:bg-slate-700 dark:text-blue-400'
              : 'text-slate-600 hover:text-slate-900 dark:text-slate-400 dark:hover:text-slate-200'
          }`}>
          <BarChart3 className="h-3 w-3" />
          <span>{t.tracer.title}</span>
        </button>
        <button
          onClick={() => onNavigate('settings')}
          className={`flex cursor-pointer items-center gap-1.5 rounded-md px-2.5 py-1 text-xs font-medium transition-colors ${
            activeRoute === 'settings'
              ? 'bg-white text-blue-600 shadow-xs dark:bg-slate-700 dark:text-blue-400'
              : 'text-slate-600 hover:text-slate-900 dark:text-slate-400 dark:hover:text-slate-200'
          }`}>
          <SettingsIcon className="h-3 w-3" />
          <span>{t.settings.title}</span>
        </button>
      </nav>

      {/* Right: Quick Controls & Window Actions */}
      <div className="titlebar-no-drag flex items-center gap-1">
        <IconButton
          aria-label={t.tracer.themeSwitch}
          variant="ghost"
          size="sm"
          onClick={toggleTheme}
          title={`${t.tracer.themeSwitch} (${theme})`}>
          {resolvedTheme === 'Dark' ? <Sun className="h-3.5 w-3.5" /> : <Moon className="h-3.5 w-3.5" />}
        </IconButton>
        <IconButton
          aria-label={t.tracer.localeSwitch}
          variant="ghost"
          size="sm"
          onClick={toggleLocale}
          title={t.tracer.localeSwitch}>
          <Languages className="h-3.5 w-3.5" />
        </IconButton>
        <div className="mx-1 h-3.5 w-px bg-slate-200 dark:bg-slate-800" />
        <IconButton
          aria-label="Minimize"
          variant="ghost"
          size="sm"
          onClick={() => bridge.sendCommand('minimizeWindow')}>
          <Minus className="h-3.5 w-3.5" />
        </IconButton>
        <IconButton
          aria-label="Maximize"
          variant="ghost"
          size="sm"
          onClick={() => bridge.sendCommand('toggleMaximize')}>
          <Square className="h-3.5 w-3.5" />
        </IconButton>
        <IconButton
          aria-label={t.common.exitApp}
          variant="ghost"
          size="sm"
          onClick={() => bridge.sendCommand('exitApp')}
          className="hover:bg-red-500 hover:text-white dark:hover:bg-red-600">
          <X className="h-3.5 w-3.5" />
        </IconButton>
      </div>
    </header>
  )
}

const AppContent: React.FC = () => {
  const [route, setRoute] = useState<Route>('capsule')

  return (
    <div className="flex h-full w-full max-h-screen max-w-screen flex-col overflow-hidden bg-slate-100/90 text-slate-900 transition-colors duration-200 select-none dark:bg-slate-950 dark:text-slate-100">
      <TitleBar activeRoute={route} onNavigate={setRoute} />
      <main className="min-h-0 flex-1 overflow-y-auto overscroll-contain p-4 pb-16">
        {route === 'capsule' && (
          <div className="mx-auto flex max-w-2xl flex-col items-center justify-start gap-8 py-8 pb-20">
            <div className="flex flex-col items-center gap-2 text-center">
              <h2 className="text-base font-bold text-slate-900 dark:text-slate-100">Dock Capsule Preview & Monitor</h2>
              <p className="max-w-md text-xs text-slate-500 dark:text-slate-400">
                Hover or click the capsule below to expand real-time traffic details. Capsule size, rounding, and timing
                sync directly with Settings.
              </p>
            </div>
            <DockCapsule onOpenSettings={() => setRoute('settings')} onOpenTracer={() => setRoute('tracer')} />
          </div>
        )}
        {route === 'tracer' && <LiveTracer onNavigateToSettings={() => setRoute('settings')} />}
        {route === 'settings' && <SettingsCenter onBack={() => setRoute('capsule')} />}
      </main>
    </div>
  )
}

export const App: React.FC = () => {
  return (
    <BridgeProvider>
      <ThemeProvider>
        <I18nProvider>
          <AppContent />
        </I18nProvider>
      </ThemeProvider>
    </BridgeProvider>
  )
}
