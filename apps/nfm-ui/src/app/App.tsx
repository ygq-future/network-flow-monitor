import React from 'react'
import {
  BridgeProvider,
  useBridge,
  useCollectorStatus,
  useRankingSnapshot,
  useSessionReset,
  useSurface,
  useTrafficTick,
} from '../bridge/BridgeContext'
import { ThemeProvider, useTheme } from '../shared/theme/ThemeContext'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { DockCapsule } from '../features/capsule/DockCapsule'
import { BasicMonitorPanel } from '../features/capsule/BasicMonitorPanel'
import { SettingsCenter } from '../features/settings/SettingsCenter'

const SettingsSurface: React.FC = () => {
  const bridge = useBridge()
  const { resolvedTheme } = useTheme()
  const isDark = resolvedTheme === 'Dark'
  const bgStyle = isDark ? '#09090b' : '#ffffff'

  return (
    <div
      style={{
        backgroundColor: bgStyle,
      }}
      className="flex h-full w-full flex-col overflow-hidden bg-white text-zinc-900 dark:bg-zinc-950 dark:text-zinc-100">
      <main className="min-h-0 flex-1 overflow-hidden">
        <SettingsCenter
          onBack={() => bridge.sendCommand('hideMonitor')}
          onStartDrag={() => bridge.sendCommand('startWindowDrag')}
        />
      </main>
    </div>
  )
}

export const ProductSurface: React.FC = () => {
  const bridge = useBridge()
  const surface = useSurface()
  const tick = useTrafficTick()
  const ranking = useRankingSnapshot()
  const status = useCollectorStatus()
  const sessionReset = useSessionReset()

  if (surface.surface === 'tray') return null
  if (surface.surface === 'settings') return <SettingsSurface />

  const isRightDock = surface.dockSide === 'right'
  const isPanelActive = surface.surface === 'panel'

  return (
    <div className="relative h-full w-full overflow-hidden bg-transparent">
      <div
        aria-hidden={isPanelActive}
        className={`fixed top-0 z-10 ${isRightDock ? 'right-0' : 'left-0'} ${
          isPanelActive ? 'pointer-events-none' : 'pointer-events-auto'
        }`}>
        <DockCapsule
          key={sessionReset?.collectorInstanceId ?? 'initial'}
          transitionPhase={isPanelActive ? 'expanding-to-panel' : 'capsule-idle'}
        />
      </div>
      <div
        aria-hidden={!isPanelActive}
        className={`fixed top-0 z-20 ${isRightDock ? 'right-0' : 'left-0'} h-full ${
          isPanelActive ? 'pointer-events-auto' : 'pointer-events-none'
        }`}>
        <BasicMonitorPanel
          tick={tick}
          ranking={ranking}
          status={status}
          isPinned={surface.pinned}
          transitionState={isPanelActive ? 'entered' : 'exiting'}
          onTogglePin={() => bridge.sendCommand('setPin', { pinned: !surface.pinned })}
          onStartDrag={() => bridge.sendCommand('startWindowDrag')}
          onOpenSettings={() => bridge.sendCommand('openSettings', { route: 'general' })}
          onClose={() => bridge.sendCommand('hideMonitor')}
          onContextMenu={event => {
            event.preventDefault()
            bridge.sendCommand('openContextMenu', { x: event.screenX, y: event.screenY })
          }}
          className="h-full w-full"
        />
      </div>
    </div>
  )
}

export const App: React.FC = () => (
  <BridgeProvider>
    <ThemeProvider>
      <I18nProvider>
        <ProductSurface />
      </I18nProvider>
    </ThemeProvider>
  </BridgeProvider>
)
