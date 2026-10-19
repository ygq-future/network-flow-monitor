import React, { useRef } from 'react'
import { useAppConfig, useBridge, useCollectorStatus, useSurface, useTrafficTick } from '../../bridge/BridgeContext'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { useTheme } from '../../shared/theme/ThemeContext'
import {
  calculateRateFillPercent,
  getRateTransition,
  MAX_REFERENCE_RATE_BYTES_PER_SEC,
  RateTransitionConfig,
} from './rateMapping'

export type CapsuleTransitionPhase = 'capsule-idle' | 'expanding-to-panel' | 'collapsing-to-capsule'

export interface DockCapsuleProps {
  transitionPhase?: CapsuleTransitionPhase
  className?: string
}

interface AnimatedFill {
  percent: number
  transition: RateTransitionConfig
}

function useAnimatedFill(rate: number, reducedMotion: boolean, adaptiveCeiling: number): AnimatedFill {
  const prevPercentRef = useRef<number>(0)
  const targetPercent = calculateRateFillPercent(rate, adaptiveCeiling) / 2
  const transition = getRateTransition(prevPercentRef.current, targetPercent, reducedMotion)
  prevPercentRef.current = targetPercent
  return {
    percent: targetPercent,
    transition,
  }
}

function mixHexColor(hex: string, targetHex: string, ratio: number): string {
  const clean = hex.replace('#', '')
  const target = targetHex.replace('#', '')
  if (clean.length !== 6 || target.length !== 6) return hex
  const r1 = parseInt(clean.slice(0, 2), 16)
  const g1 = parseInt(clean.slice(2, 4), 16)
  const b1 = parseInt(clean.slice(4, 6), 16)
  const r2 = parseInt(target.slice(0, 2), 16)
  const g2 = parseInt(target.slice(2, 4), 16)
  const b2 = parseInt(target.slice(4, 6), 16)
  const r = Math.round(r1 + (r2 - r1) * ratio)
  const g = Math.round(g1 + (g2 - g1) * ratio)
  const b = Math.round(b1 + (b2 - b1) * ratio)
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`
}

export const DockCapsule: React.FC<DockCapsuleProps> = ({ transitionPhase = 'capsule-idle', className = '' }) => {
  const { t } = useTranslation()
  const { resolvedTheme, prefersReducedMotion } = useTheme()
  const bridge = useBridge()
  const tick = useTrafficTick()
  const status = useCollectorStatus()
  const surface = useSurface()
  const config = useAppConfig()
  const peakCeilingRef = useRef<number>(10 * 1024 * 1024)

  const connected = status.state === 'Running' || status.etwSessionActive
  const download = connected ? tick.downloadBytesPerSec : 0
  const upload = connected ? tick.uploadBytesPerSec : 0

  const currentPeak = Math.max(download, upload)
  if (currentPeak > peakCeilingRef.current) {
    peakCeilingRef.current = Math.min(MAX_REFERENCE_RATE_BYTES_PER_SEC, currentPeak * 1.5)
  } else {
    peakCeilingRef.current = Math.max(10 * 1024 * 1024, peakCeilingRef.current * 0.98)
  }

  const widthDip = config.capsule.widthDip
  const heightDip = config.capsule.heightDip
  const roundingDip = Math.min(config.capsule.effectiveEndRoundingDip, widthDip / 2)
  const downloadFill = useAnimatedFill(download, prefersReducedMotion, peakCeilingRef.current)
  const uploadFill = useAnimatedFill(upload, prefersReducedMotion, peakCeilingRef.current)

  const isDark = resolvedTheme === 'Dark'
  const downBaseHex = config.capsule.downloadColorHex || '#38bdf8'
  const upBaseHex = config.capsule.uploadColorHex || '#2dd4bf'
  const downLightHex = mixHexColor(downBaseHex, '#ffffff', 0.35)
  const upLightHex = mixHexColor(upBaseHex, '#ffffff', 0.35)

  const downGradient = `linear-gradient(to bottom, ${downLightHex} 0%, ${downBaseHex} 100%)`
  const upGradient = `linear-gradient(to top, ${upLightHex} 0%, ${upBaseHex} 100%)`

  // When autoHidden is active, retract capsule into screen edge leaving a 4px strip with neutral capsule base
  const isRightDock = surface.dockSide === 'right'
  const autoHideOffsetPx = surface.autoHidden ? Math.max(0, widthDip - 4) : 0
  const translateX = surface.autoHidden ? (isRightDock ? autoHideOffsetPx : -autoHideOffsetPx) : 0
  const transformOrigin = isRightDock ? 'right top' : 'left top'
  const isExpandingOut = transitionPhase === 'expanding-to-panel'
  const hitWidthDip = Math.max(widthDip, 16)
  const morphScaleX = isExpandingOut ? 1.8 : 1
  const morphScaleY = isExpandingOut ? 1.2 : 1
  const morphOpacity = isExpandingOut ? 0 : 1
  const morphDurationMs = prefersReducedMotion ? 0 : isExpandingOut ? 0 : 220

  return (
    <button
      type="button"
      aria-label={t.common.appName}
      onMouseDown={event => {
        if (event.button === 0) {
          bridge.sendCommand('startWindowDrag')
        }
      }}
      onKeyDown={event => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault()
          bridge.sendCommand('showMonitor')
        }
      }}
      onContextMenu={event => {
        event.preventDefault()
        bridge.sendCommand('openContextMenu', { x: event.screenX, y: event.screenY })
      }}
      style={{
        width: `${hitWidthDip}px`,
        height: `${heightDip}px`,
      }}
      className={`relative flex cursor-pointer items-start overflow-visible bg-transparent outline-hidden ${isRightDock ? 'justify-end' : 'justify-start'} ${className}`}>
      {!connected && <span className="sr-only">{t.common.disconnected}</span>}
      <span
        data-testid="capsule-visual"
        data-connected={connected}
        className="relative block overflow-hidden p-0"
        style={{
          width: `${widthDip}px`,
          height: `${heightDip}px`,
          borderRadius: `${roundingDip}px`,
          backgroundColor: isDark ? '#09090b' : '#ffffff',
          transformOrigin,
          transform: `translateX(${translateX}px) scale(${morphScaleX}, ${morphScaleY})`,
          opacity: morphOpacity,
          transition:
            morphDurationMs === 0
              ? 'none'
              : `transform ${morphDurationMs}ms cubic-bezier(0.22, 1, 0.36, 1), opacity ${Math.round(morphDurationMs * 0.85)}ms ease-out`,
        }}>
        <span
          className="relative block h-full w-full overflow-hidden"
          style={{
            borderRadius: `${roundingDip}px`,
            clipPath: `inset(0 round ${roundingDip}px)`,
          }}>
          <span
            data-testid="download-fill"
            className="absolute inset-x-0 top-0 block"
            style={{
              height: `${downloadFill.percent}%`,
              borderTopLeftRadius: `${roundingDip}px`,
              borderTopRightRadius: `${roundingDip}px`,
              borderBottomLeftRadius: `${roundingDip}px`,
              borderBottomRightRadius: `${roundingDip}px`,
              background: downGradient,
              transition: `height ${downloadFill.transition.durationMs}ms ${downloadFill.transition.easing}`,
            }}
          />
          <span
            data-testid="upload-fill"
            className="absolute inset-x-0 bottom-0 block"
            style={{
              height: `${uploadFill.percent}%`,
              borderTopLeftRadius: `${roundingDip}px`,
              borderTopRightRadius: `${roundingDip}px`,
              borderBottomLeftRadius: `${roundingDip}px`,
              borderBottomRightRadius: `${roundingDip}px`,
              background: upGradient,
              transition: `height ${uploadFill.transition.durationMs}ms ${uploadFill.transition.easing}`,
            }}
          />
        </span>
      </span>
    </button>
  )
}
