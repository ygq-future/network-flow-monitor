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
  if (surface.surface === 'capsule') return <DockCapsule key={sessionReset?.collectorInstanceId ?? 'initial'} />
  if (surface.surface === 'settings') return <SettingsSurface />

  return (
    <BasicMonitorPanel
      tick={tick}
      ranking={ranking}
      status={status}
      isPinned={surface.pinned}
      onTogglePin={() => bridge.sendCommand('setPin', { pinned: !surface.pinned })}
      onStartDrag={() => bridge.sendCommand('startWindowDrag')}
      onOpenSettings={() => bridge.sendCommand('openSettings', { route: 'general' })}
      onClose={() => bridge.sendCommand('hideMonitor')}
      onContextMenu={event => {
        event.preventDefault()
        bridge.sendCommand('openContextMenu', { x: event.screenX, y: event.screenY })
      }}
      className="h-full w-full rounded-none border-0"
    />
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
