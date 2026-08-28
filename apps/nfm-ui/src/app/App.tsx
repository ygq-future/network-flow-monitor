import React, { useState } from 'react'
import { ThemeProvider } from '../shared/theme/ThemeContext'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { BridgeProvider, useBridge } from '../bridge/BridgeContext'
import { LiveTracer } from '../features/tracer/LiveTracer'
import { SettingsPlaceholder } from '../features/settings/SettingsPlaceholder'

type Route = 'tracer' | 'settings'

const AppContent: React.FC = () => {
  const [route, setRoute] = useState<Route>('tracer')
  const bridge = useBridge()

  React.useEffect(() => {
    const unsub = bridge.on('configChanged', () => {
      // Future settings update hook
    })
    return unsub
  }, [bridge])

  return (
    <main className="min-h-screen bg-slate-100 transition-colors duration-200 dark:bg-slate-950">
      {route === 'tracer' ? (
        <LiveTracer onNavigateToSettings={() => setRoute('settings')} />
      ) : (
        <SettingsPlaceholder onBackToTracer={() => setRoute('tracer')} />
      )}
    </main>
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
