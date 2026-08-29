import React, { useState, useEffect } from 'react'
import { ArrowDown, ArrowUp, Settings as SettingsIcon, BarChart3, Layers, Pin, PinOff } from 'lucide-react'
import { useBridge, useTrafficTick, useCollectorStatus } from '../../bridge/BridgeContext'
import { AppConfigDto, ConfigChangedDto } from '../../bridge/bridgeTypes'
import { defaultMockConfigChanged } from '../../bridge/nativeBridge'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { formatRate } from '../../shared/utils/formatRate'
import { Button } from '../../shared/components/Button'
import { IconButton } from '../../shared/components/IconButton'
import { Badge } from '../../shared/components/Badge'
import { Card } from '../../shared/components/Card'

export interface DockCapsuleProps {
  onOpenSettings?: () => void
  onOpenTracer?: () => void
  className?: string
}

export const DockCapsule: React.FC<DockCapsuleProps> = ({ onOpenSettings, onOpenTracer, className = '' }) => {
  const { t } = useTranslation()
  const bridge = useBridge()
  const tick = useTrafficTick()
  const status = useCollectorStatus()

  const [config, setConfig] = useState<AppConfigDto>(defaultMockConfigChanged.config)
  const [isExpanded, setIsExpanded] = useState(false)
  const [isHovered, setIsHovered] = useState(false)
  const [isPinned, setIsPinned] = useState(false)

  useEffect(() => {
    const unsub = bridge.on<ConfigChangedDto>('configChanged', data => {
      if (data?.config) {
        setConfig(data.config)
        setIsPinned(data.config.monitor.pinned)
      }
    })
    bridge.sendCommand('requestConfig')
    return unsub
  }, [bridge])

  // Calculate dynamic fill percentages for download (top-down) and upload (bottom-up)
  const dlPercent = Math.min(100, Math.max(4, Math.log10(Math.max(1, tick.downloadBytesPerSec) + 1) * 15))
  const ulPercent = Math.min(100, Math.max(4, Math.log10(Math.max(1, tick.uploadBytesPerSec) + 1) * 15))

  const capsuleWidth = config.capsule.widthDip || 14
  const capsuleHeight = config.capsule.heightDip || 80
  const endRounding = config.capsule.effectiveEndRoundingDip || Math.min(6, Math.floor(capsuleWidth / 2))

  const handleTogglePin = () => {
    const nextPin = !isPinned
    setIsPinned(nextPin)
    bridge.sendCommand('updateSetting', { key: 'floating.nfm_pinned', value: nextPin })
  }

  const showPopover = isExpanded || isPinned || isHovered

  return (
    <div className={`relative flex items-center select-none ${className}`}>
      {/* 1. Main Capsule Bar */}
      <div
        role="button"
        tabIndex={0}
        aria-label={t.common.appName}
        onClick={() => setIsExpanded(!isExpanded)}
        onMouseEnter={() => setIsHovered(true)}
        onMouseLeave={() => setIsHovered(false)}
        style={{
          width: `${Math.max(12, capsuleWidth)}px`,
          height: `${Math.max(60, capsuleHeight)}px`,
          borderRadius: `${endRounding}px`,
        }}
        className="group relative cursor-pointer overflow-hidden border border-slate-300/80 bg-slate-900/90 shadow-lg backdrop-blur-md transition-all duration-200 hover:scale-105 hover:border-blue-400 dark:border-slate-700/80 dark:bg-slate-950/90">
        {/* Download Flow Fill (Top -> Down) */}
        <div
          style={{ height: `${dlPercent}%` }}
          className="absolute top-0 right-0 left-0 bg-gradient-to-b from-blue-500 via-cyan-400 to-emerald-400 opacity-90 transition-all duration-300 ease-out"
        />

        {/* Upload Flow Fill (Bottom -> Up) */}
        <div
          style={{ height: `${ulPercent}%` }}
          className="absolute right-0 bottom-0 left-0 bg-gradient-to-t from-purple-500 via-indigo-400 to-sky-400 opacity-90 transition-all duration-300 ease-out"
        />

        {/* Center Divider / Active Core */}
        <div className="absolute top-1/2 right-0 left-0 h-0.5 -translate-y-1/2 bg-white/40 shadow-xs" />
      </div>

      {/* 2. Side Floating Rate Indicator Pill */}
      <div
        onClick={() => setIsExpanded(!isExpanded)}
        className="ml-3 flex cursor-pointer items-center gap-3 rounded-full border border-slate-200/80 bg-white/80 px-3.5 py-1.5 shadow-md backdrop-blur-md transition-all duration-200 hover:border-blue-300 hover:shadow-lg dark:border-slate-800 dark:bg-slate-900/80">
        <div className="flex items-center gap-1.5 text-xs font-bold text-blue-600 dark:text-blue-400">
          <ArrowDown className="h-3.5 w-3.5 stroke-[2.5]" />
          <span className="font-mono">{formatRate(tick.downloadBytesPerSec).full}</span>
        </div>
        <div className="h-3 w-px bg-slate-300 dark:bg-slate-700" />
        <div className="flex items-center gap-1.5 text-xs font-bold text-purple-600 dark:text-purple-400">
          <ArrowUp className="h-3.5 w-3.5 stroke-[2.5]" />
          <span className="font-mono">{formatRate(tick.uploadBytesPerSec).full}</span>
        </div>
        <Badge variant={status.etwSessionActive ? 'success' : 'warning'} className="text-[10px]">
          {status.activePids} PIDs
        </Badge>
      </div>

      {/* 3. Expanded Monitor Details Popover Card */}
      {showPopover && (
        <div
          onMouseEnter={() => setIsHovered(true)}
          onMouseLeave={() => setIsHovered(false)}
          className="animate-in fade-in zoom-in-95 absolute top-full left-0 z-50 mt-3 w-80 duration-150">
          <Card className="flex flex-col gap-3.5 border-slate-200/90 p-4 shadow-xl dark:border-slate-800">
            {/* Popover Header */}
            <div className="flex items-center justify-between border-b border-slate-100 pb-2.5 dark:border-slate-800/80">
              <div className="flex items-center gap-2">
                <Layers className="h-4 w-4 text-blue-600 dark:text-blue-400" />
                <span className="text-xs font-bold text-slate-900 dark:text-slate-100">{t.common.appName}</span>
              </div>
              <div className="flex items-center gap-1">
                <IconButton
                  aria-label={isPinned ? t.common.unpin : t.common.pin}
                  variant={isPinned ? 'primary' : 'ghost'}
                  size="sm"
                  onClick={handleTogglePin}>
                  {isPinned ? <PinOff className="h-3.5 w-3.5" /> : <Pin className="h-3.5 w-3.5" />}
                </IconButton>
                {onOpenSettings && (
                  <IconButton
                    aria-label={t.common.settings}
                    variant="ghost"
                    size="sm"
                    onClick={() => {
                      setIsExpanded(false)
                      onOpenSettings()
                    }}>
                    <SettingsIcon className="h-3.5 w-3.5" />
                  </IconButton>
                )}
              </div>
            </div>

            {/* Rates Grid */}
            <div className="grid grid-cols-2 gap-2">
              <div className="flex flex-col rounded-lg bg-blue-50/60 p-2.5 dark:bg-blue-950/30">
                <div className="flex items-center gap-1 text-[11px] font-medium text-blue-600 dark:text-blue-400">
                  <ArrowDown className="h-3 w-3" />
                  <span>{t.common.download}</span>
                </div>
                <span className="mt-1 font-mono text-sm font-bold text-slate-900 dark:text-slate-100">
                  {formatRate(tick.downloadBytesPerSec).full}
                </span>
              </div>
              <div className="flex flex-col rounded-lg bg-purple-50/60 p-2.5 dark:bg-purple-950/30">
                <div className="flex items-center gap-1 text-[11px] font-medium text-purple-600 dark:text-purple-400">
                  <ArrowUp className="h-3 w-3" />
                  <span>{t.common.upload}</span>
                </div>
                <span className="mt-1 font-mono text-sm font-bold text-slate-900 dark:text-slate-100">
                  {formatRate(tick.uploadBytesPerSec).full}
                </span>
              </div>
            </div>

            {/* Quick Actions & Navigation */}
            <div className="flex items-center gap-2 pt-1">
              {onOpenTracer && (
                <Button variant="secondary" size="sm" onClick={onOpenTracer} className="flex-1 gap-1 text-xs">
                  <BarChart3 className="h-3.5 w-3.5" />
                  <span>{t.tracer.title}</span>
                </Button>
              )}
              {onOpenSettings && (
                <Button variant="primary" size="sm" onClick={onOpenSettings} className="flex-1 gap-1 text-xs">
                  <SettingsIcon className="h-3.5 w-3.5" />
                  <span>{t.settings.title}</span>
                </Button>
              )}
            </div>
          </Card>
        </div>
      )}
    </div>
  )
}
