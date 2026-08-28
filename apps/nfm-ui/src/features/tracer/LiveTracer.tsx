import React from 'react'
import { Activity, ShieldCheck, Moon, Sun, Globe, Settings } from 'lucide-react'
import { Card } from '../../shared/components/Card'
import { Badge } from '../../shared/components/Badge'
import { Button } from '../../shared/components/Button'
import { Select } from '../../shared/components/Select'
import { LiveRateDisplay } from './LiveRateDisplay'
import { useBridge, useCollectorStatus, useSessionReset } from '../../bridge/BridgeContext'
import { useTheme } from '../../shared/theme/ThemeContext'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { ThemeMode } from '../../shared/theme/themeTokens'
import { LocaleMode } from '../../shared/i18n/locales'

export interface LiveTracerProps {
  onNavigateToSettings?: () => void
}

export const LiveTracer: React.FC<LiveTracerProps> = ({ onNavigateToSettings }) => {
  const bridge = useBridge()
  const status = useCollectorStatus()
  const sessionReset = useSessionReset()
  const { theme, resolvedTheme, setTheme } = useTheme()
  const { locale, setLocale, t } = useTranslation()

  const handleThemeChange = (newTheme: ThemeMode) => {
    setTheme(newTheme)
    bridge.sendCommand('setTheme', { theme: newTheme })
  }

  const handleLocaleChange = (newLocale: LocaleMode) => {
    setLocale(newLocale)
    bridge.sendCommand('setLocale', { locale: newLocale })
  }

  return (
    <div className="mx-auto flex max-w-lg flex-col gap-4 p-4 select-none">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2.5">
          <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-blue-600 text-white shadow-xs">
            <Activity className="h-5 w-5" />
          </div>
          <div>
            <h1 className="text-sm font-bold text-slate-900 dark:text-slate-100">{t.tracer.title}</h1>
            <p className="text-[11px] text-slate-500 dark:text-slate-400">{t.tracer.subtitle}</p>
          </div>
        </div>

        <Badge variant={status.etwSessionActive ? 'success' : 'warning'}>
          <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-current" />
          {status.etwSessionActive ? t.tracer.active : t.tracer.inactive}
        </Badge>
      </div>

      {/* Live Rate Stream */}
      <LiveRateDisplay />

      {/* Collector Status & Diagnostics */}
      <Card className="flex flex-col gap-2.5">
        <div className="flex items-center justify-between text-xs font-semibold text-slate-700 dark:text-slate-300">
          <div className="flex items-center gap-1.5">
            <ShieldCheck className="h-4 w-4 text-blue-500" />
            <span>{t.tracer.collectorHealth}</span>
          </div>
          {sessionReset && (
            <span className="font-mono text-[10px] text-slate-400">{sessionReset.collectorInstanceId}</span>
          )}
        </div>

        <div className="grid grid-cols-2 gap-2 text-xs">
          <div className="flex justify-between rounded-lg bg-slate-100/60 p-2 dark:bg-slate-800/40">
            <span className="text-slate-500 dark:text-slate-400">{t.common.activeProcesses}:</span>
            <span className="font-mono font-medium text-slate-900 dark:text-slate-100">{status.activePids}</span>
          </div>
          <div className="flex justify-between rounded-lg bg-slate-100/60 p-2 dark:bg-slate-800/40">
            <span className="text-slate-500 dark:text-slate-400">{t.common.droppedPackets}:</span>
            <span className="font-mono font-medium text-slate-900 dark:text-slate-100">{status.droppedPackets}</span>
          </div>
        </div>
      </Card>

      {/* Runtime Theme & Locale Controls */}
      <Card className="flex flex-col gap-3">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2 text-xs font-medium text-slate-700 dark:text-slate-300">
            {resolvedTheme === 'Dark' ? (
              <Moon className="h-4 w-4 text-blue-400" />
            ) : (
              <Sun className="h-4 w-4 text-amber-500" />
            )}
            <span>{t.tracer.themeSwitch}</span>
          </div>
          <Select<ThemeMode>
            value={theme}
            onChange={handleThemeChange}
            options={[
              { value: 'System', label: t.tracer.themeSystem },
              { value: 'Light', label: t.tracer.themeLight },
              { value: 'Dark', label: t.tracer.themeDark },
            ]}
          />
        </div>

        <div className="flex items-center justify-between border-t border-slate-200/60 pt-3 dark:border-slate-800/60">
          <div className="flex items-center gap-2 text-xs font-medium text-slate-700 dark:text-slate-300">
            <Globe className="h-4 w-4 text-indigo-500" />
            <span>{t.tracer.localeSwitch}</span>
          </div>
          <Select<LocaleMode>
            value={locale}
            onChange={handleLocaleChange}
            options={[
              { value: 'System', label: t.tracer.localeSystem },
              { value: 'English', label: t.tracer.localeEnglish },
              { value: 'SimplifiedChinese', label: t.tracer.localeChinese },
            ]}
          />
        </div>
      </Card>

      {/* Navigation placeholder to Settings (Ticket 02 seam) */}
      <Button
        variant="outline"
        size="sm"
        onClick={onNavigateToSettings}
        className="w-full justify-center gap-2 text-xs">
        <Settings className="h-3.5 w-3.5" />
        {t.tracer.navToSettings}
      </Button>
    </div>
  )
}
