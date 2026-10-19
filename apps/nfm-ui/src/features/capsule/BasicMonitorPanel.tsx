import React, { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { Pin, PinOff, Settings as SettingsIcon, AlertCircle, AppWindow } from 'lucide-react'
import { TrafficTickDto, CollectorStatusDto, RankingSnapshotDto } from '../../bridge/bridgeTypes'
import { useAppConfig, useBridge, useSurface } from '../../bridge/BridgeContext'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { useTheme } from '../../shared/theme/ThemeContext'
import { formatRate } from '../../shared/utils/formatRate'
import { IconButton } from '../../shared/components/IconButton'
import { AppLogo } from '../../shared/components/AppLogo'
import { calculateRateFillPercent, MAX_REFERENCE_RATE_BYTES_PER_SEC } from './rateMapping'

export type MonitorPanelTransitionState = 'entering' | 'entered' | 'exiting'

export interface BasicMonitorPanelProps {
  tick: TrafficTickDto
  status: CollectorStatusDto
  ranking?: RankingSnapshotDto
  isPinned: boolean
  transitionState?: MonitorPanelTransitionState
  onTogglePin: () => void
  onStartDrag?: () => void
  onOpenSettings?: () => void
  onOpenTracer?: () => void
  onClose?: () => void
  onContextMenu?: (e: React.MouseEvent) => void
  className?: string
}

function formatCompactRate(bytesPerSec: number, unitMode: 'auto' | 'bytes' | 'bits' = 'auto'): string {
  if (bytesPerSec <= 0) return '0 B/s'
  const formatted = formatRate(bytesPerSec, unitMode)
  return `${formatted.value} ${formatted.unit}`
}

export const BasicMonitorPanel: React.FC<BasicMonitorPanelProps> = ({
  tick,
  status,
  ranking,
  isPinned,
  transitionState,
  onTogglePin,
  onStartDrag,
  onOpenSettings,
  onContextMenu,
  className = '',
}) => {
  const { t, resolvedLocale } = useTranslation()
  const { resolvedTheme, prefersReducedMotion } = useTheme()
  const bridge = useBridge()
  const surface = useSurface()
  const config = useAppConfig()
  const isZh = resolvedLocale === 'SimplifiedChinese'
  const isDark = resolvedTheme === 'Dark'

  const [selfEntered, setSelfEntered] = useState(false)
  useEffect(() => {
    const id = requestAnimationFrame(() => setSelfEntered(true))
    return () => cancelAnimationFrame(id)
  }, [])
  const activeTransitionState: MonitorPanelTransitionState = transitionState ?? (selfEntered ? 'entered' : 'entering')
  const isCollectorRunning = status.state === 'Running' || status.etwSessionActive
  const speedUnit = config.general.speedUnit
  const downloadColorHex = config.capsule.downloadColorHex || '#38bdf8'
  const uploadColorHex = config.capsule.uploadColorHex || '#2dd4bf'
  const hasReadyRanking = ranking !== undefined && ranking.status === 'Ready'
  const totalDownloadBytesPerSec = hasReadyRanking ? ranking.totalDownloadBytesPerSec : tick.downloadBytesPerSec
  const totalUploadBytesPerSec = hasReadyRanking ? ranking.totalUploadBytesPerSec : tick.uploadBytesPerSec
  const rawApps = ranking?.applications ?? []
  const apps = rawApps.slice(0, Math.max(1, config.ranking.topN))
  const peakCeilingRef = useRef<number>(10 * 1024 * 1024)
  const currentPeak = apps.reduce(
    (max, app) => Math.max(max, app.downloadBytesPerSec + app.uploadBytesPerSec),
    Math.max(totalDownloadBytesPerSec, totalUploadBytesPerSec),
  )
  if (currentPeak > peakCeilingRef.current) {
    peakCeilingRef.current = Math.min(MAX_REFERENCE_RATE_BYTES_PER_SEC, currentPeak * 1.5)
  } else {
    peakCeilingRef.current = Math.max(10 * 1024 * 1024, peakCeilingRef.current * 0.98)
  }

  // Measure & report dynamic height to native host window
  const panelRef = useRef<HTMLDivElement>(null)
  const contentHeightDip = !isCollectorRunning || apps.length === 0 ? 44 : apps.length * 32
  const desiredHeightDip = Math.min(520, Math.max(80, 32 + contentHeightDip + 8))
  useLayoutEffect(() => {
    if (activeTransitionState !== 'exiting') {
      bridge.sendCommand('resizePanel', { heightDip: desiredHeightDip })
    }
  }, [bridge, desiredHeightDip, activeTransitionState])
  // FLIP animation for Top N application rows when ranking order changes
  const rowRefs = useRef<Map<string, HTMLDivElement>>(new Map())
  const prevTopsRef = useRef<Map<string, number>>(new Map())
  const appOrderSignature = apps.map(a => a.id || a.displayName).join('|')

  useLayoutEffect(() => {
    const nextTops = new Map<string, number>()
    rowRefs.current.forEach((el, key) => {
      nextTops.set(key, el.getBoundingClientRect().top)
    })
    nextTops.forEach((newTop, key) => {
      const oldTop = prevTopsRef.current.get(key)
      const el = rowRefs.current.get(key)
      if (oldTop !== undefined && el) {
        const deltaY = oldTop - newTop
        if (Math.abs(deltaY) > 1) {
          el.style.transition = 'none'
          el.style.transform = `translateY(${deltaY}px)`
          requestAnimationFrame(() => {
            el.style.transition = 'transform 340ms cubic-bezier(0.22, 1, 0.36, 1)'
            el.style.transform = 'translateY(0)'
          })
        }
      }
    })
    prevTopsRef.current = nextTops
  }, [appOrderSignature])

  const panelBg = isDark ? '#09090b' : '#ffffff'
  const transformOrigin = surface.dockSide === 'left' ? 'left top' : 'right top'
  const capsuleScaleX = Math.max(0.03, Math.min(0.18, config.capsule.widthDip / 340))
  const capsuleScaleY = Math.max(0.16, Math.min(0.75, config.capsule.heightDip / Math.max(80, desiredHeightDip)))
  const isCollapsedToCapsule = activeTransitionState !== 'entered' && !prefersReducedMotion
  const scaleX = isCollapsedToCapsule ? capsuleScaleX : 1
  const scaleY = isCollapsedToCapsule ? capsuleScaleY : 1
  const panelOpacity = isCollapsedToCapsule ? 0 : 1
  const panelBorderRadiusPx = isCollapsedToCapsule ? 32 : 12
  const morphTransition = prefersReducedMotion
    ? 'none'
    : activeTransitionState === 'exiting'
      ? 'transform 260ms cubic-bezier(0.36, 0, 0.66, -0.05), opacity 220ms cubic-bezier(0.4, 0, 1, 1), border-radius 260ms ease-in, height 240ms cubic-bezier(0.22, 1, 0.36, 1)'
      : 'transform 320ms cubic-bezier(0.16, 1, 0.3, 1), opacity 180ms ease-out, border-radius 320ms cubic-bezier(0.16, 1, 0.3, 1), height 240ms cubic-bezier(0.22, 1, 0.36, 1)'
  return (
    <div
      ref={panelRef}
      role="region"
      aria-label={t.settings.monitor}
      onContextMenu={onContextMenu}
      style={{
        backgroundColor: panelBg,
        height: `${desiredHeightDip}px`,
        transformOrigin,
        transform: `scale(${scaleX.toFixed(4)}, ${scaleY.toFixed(4)})`,
        opacity: panelOpacity,
        borderRadius: `${panelBorderRadiusPx}px`,
        transition: morphTransition,
      }}
      className={`flex w-[340px] max-w-[340px] min-w-[340px] flex-col overflow-hidden border border-zinc-200 px-3 pt-1.5 pb-1 text-zinc-900 select-none dark:border-zinc-800 dark:text-zinc-100 ${className}`}>
      {/* Compact Single-Line Draggable Header: Title + Total Speeds + Actions */}
      <div
        data-testid="total-traffic-row"
        onMouseDown={event => {
          if (event.button === 0 && !(event.target as HTMLElement).closest('button')) {
            onStartDrag?.()
          }
        }}
        className="flex h-7 shrink-0 cursor-move items-center justify-between gap-1.5 border-b border-zinc-200 pb-1.5 dark:border-zinc-800">
        <div className="flex shrink-0 items-center gap-1.5">
          <AppLogo className="h-4 w-4 shrink-0" />
          <span className="text-xs font-semibold tracking-tight whitespace-nowrap text-zinc-900 dark:text-zinc-100">
            {t.common.appName}
          </span>
        </div>

        <div className="flex shrink-0 items-center gap-1.5">
          <div
            className="flex w-[66px] shrink-0 items-center justify-end gap-0.5 whitespace-nowrap"
            style={{ color: downloadColorHex }}>
            <span className="font-mono text-[11px] font-semibold">↓</span>
            <span data-testid="total-download-rate" className="font-mono text-[11px] font-semibold tabular-nums">
              {formatCompactRate(totalDownloadBytesPerSec, speedUnit)}
            </span>
          </div>
          <div
            className="flex w-[66px] shrink-0 items-center justify-end gap-0.5 whitespace-nowrap"
            style={{ color: uploadColorHex }}>
            <span className="font-mono text-[11px] font-semibold">↑</span>
            <span data-testid="total-upload-rate" className="font-mono text-[11px] font-semibold tabular-nums">
              {formatCompactRate(totalUploadBytesPerSec, speedUnit)}
            </span>
          </div>
        </div>

        <div className="flex shrink-0 items-center gap-0.5">
          <IconButton
            aria-label={isPinned ? t.common.unpin : t.common.pin}
            variant={isPinned ? 'primary' : 'ghost'}
            size="sm"
            onClick={onTogglePin}
            title={isPinned ? t.common.unpin : t.common.pin}>
            {isPinned ? <PinOff className="h-3.5 w-3.5" /> : <Pin className="h-3.5 w-3.5" />}
          </IconButton>
          {onOpenSettings && (
            <IconButton
              aria-label={t.common.settings}
              variant="ghost"
              size="sm"
              onClick={onOpenSettings}
              title={t.common.settings}>
              <SettingsIcon className="h-3.5 w-3.5" />
            </IconButton>
          )}
        </div>
      </div>

      {/* Dynamic Top N Application Rows */}
      <div
        style={{
          height: `${contentHeightDip}px`,
          transition: prefersReducedMotion ? 'none' : 'height 240ms cubic-bezier(0.22, 1, 0.36, 1)',
        }}
        className="mt-0.5 flex flex-col overflow-hidden">
        {!isCollectorRunning ? (
          <div className="flex h-11 items-center justify-center gap-1.5 px-3 text-center">
            <AlertCircle className="h-3.5 w-3.5 text-amber-500" />
            <span className="text-[11px] font-medium text-zinc-600 dark:text-zinc-300">{t.common.disconnected}</span>
          </div>
        ) : apps.length === 0 ? (
          <div className="flex h-11 items-center justify-center gap-1.5 px-3 text-center">
            <AppWindow className="h-3.5 w-3.5 text-zinc-400 dark:text-zinc-500" />
            <span className="text-[11px] text-zinc-500 dark:text-zinc-400">
              {ranking?.status === 'WarmingUp'
                ? isZh
                  ? '正在预热采集...'
                  : 'Warming up...'
                : isZh
                  ? '当前无活跃应用流量'
                  : 'No active application traffic'}
            </span>
          </div>
        ) : (
          apps.map((app, index) => {
            const rowKey = app.id || `${app.displayName}-${index}`
            const totalAppBytes = app.downloadBytesPerSec + app.uploadBytesPerSec
            const barPercent = calculateRateFillPercent(totalAppBytes, peakCeilingRef.current)
            const downloadSharePercent = totalAppBytes > 0 ? (app.downloadBytesPerSec / totalAppBytes) * 100 : 0
            const uploadSharePercent = totalAppBytes > 0 ? (app.uploadBytesPerSec / totalAppBytes) * 100 : 0
            const initial = (app.displayName || '?').trim().charAt(0).toUpperCase()
            const hasDataIcon = Boolean(app.iconKey && app.iconKey.startsWith('data:image/'))

            return (
              <div
                key={rowKey}
                ref={el => {
                  if (el) {
                    rowRefs.current.set(rowKey, el)
                  } else {
                    rowRefs.current.delete(rowKey)
                  }
                }}
                className="relative grid h-8 shrink-0 grid-cols-[1fr_88px_88px] items-center gap-1 rounded-md px-1.5 text-xs transition-colors hover:bg-zinc-100 dark:hover:bg-zinc-900">
                <div
                  data-testid="app-rate-bar"
                  className="pointer-events-none absolute bottom-0 left-1.5 flex h-[1.5px] overflow-hidden rounded-full opacity-55 transition-all duration-300"
                  style={{ width: `calc(${barPercent.toFixed(2)}% - 12px)` }}>
                  {downloadSharePercent > 0 && (
                    <span
                      data-testid="app-download-bar"
                      className="block h-full transition-all duration-300"
                      style={{ width: `${downloadSharePercent.toFixed(2)}%`, backgroundColor: downloadColorHex }}
                    />
                  )}
                  {uploadSharePercent > 0 && (
                    <span
                      data-testid="app-upload-bar"
                      className="block h-full transition-all duration-300"
                      style={{ width: `${uploadSharePercent.toFixed(2)}%`, backgroundColor: uploadColorHex }}
                    />
                  )}
                </div>
                <div className="flex min-w-0 items-center gap-2 pr-1">
                  {hasDataIcon ? (
                    <img src={app.iconKey} alt="" className="h-4 w-4 shrink-0 rounded-xs object-contain" />
                  ) : (
                    <span className="flex h-4 w-4 shrink-0 items-center justify-center rounded-xs border border-black/[0.08] bg-zinc-100 text-[9px] font-bold text-zinc-700 dark:border-white/[0.1] dark:bg-zinc-800 dark:text-zinc-200">
                      {initial}
                    </span>
                  )}
                  <span
                    className="truncate text-[11px] font-medium text-zinc-800 dark:text-zinc-200"
                    title={app.executablePath || app.displayName}>
                    {app.displayName}
                  </span>
                </div>
                <span className="text-right font-mono text-[11px] font-medium text-zinc-900 tabular-nums dark:text-zinc-100">
                  {formatCompactRate(app.downloadBytesPerSec, speedUnit)}
                </span>
                <span className="text-right font-mono text-[11px] text-zinc-500 tabular-nums dark:text-zinc-400">
                  {formatCompactRate(app.uploadBytesPerSec, speedUnit)}
                </span>
              </div>
            )
          })
        )}
      </div>
    </div>
  )
}
