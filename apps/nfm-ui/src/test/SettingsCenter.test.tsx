import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { SettingsCenter } from '../features/settings/SettingsCenter'
import { BridgeProvider } from '../bridge/BridgeContext'
import { MockBridge } from '../bridge/nativeBridge'
import { ThemeProvider } from '../shared/theme/ThemeContext'
import { I18nProvider } from '../shared/i18n/I18nContext'
import React from 'react'

function renderWithProviders(ui: React.ReactElement, bridge: MockBridge) {
  return render(
    <BridgeProvider bridge={bridge}>
      <ThemeProvider>
        <I18nProvider>{ui}</I18nProvider>
      </ThemeProvider>
    </BridgeProvider>,
  )
}

describe('SettingsCenter', () => {
  it('renders all 7 tabs and default general section', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    expect(screen.getByText('Settings')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /General/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Appearance/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Dock Capsule/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Monitor Panel/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Ranking/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Collector/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Diagnostics/i })).toBeInTheDocument()

    // General tab content
    expect(screen.getByText('Launch at Windows startup')).toBeInTheDocument()
    expect(screen.getByText('Speed Unit')).toBeInTheDocument()
  })

  it('switches tabs to Appearance, Capsule, Monitor, Ranking, Collector, Diagnostics', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    // 1. Appearance tab
    fireEvent.click(screen.getByRole('button', { name: /Appearance/i }))
    expect(screen.getByText('Application Theme')).toBeInTheDocument()
    expect(screen.getByText('Language')).toBeInTheDocument()
    expect(screen.getByText('Reduced Motion')).toBeInTheDocument()

    // 2. Capsule tab
    fireEvent.click(screen.getByRole('button', { name: /Dock Capsule/i }))
    expect(screen.getByText('Capsule Width')).toBeInTheDocument()
    expect(screen.getByText('Capsule Height')).toBeInTheDocument()
    expect(screen.getByText('End Rounding')).toBeInTheDocument()
    expect(screen.getByText('Hover Expand Delay')).toBeInTheDocument()
    expect(screen.getByText('Capsule Rate Colors')).toBeInTheDocument()
    // 3. Monitor tab
    fireEvent.click(screen.getByRole('button', { name: /Monitor Panel/i }))
    expect(screen.getByText('Pin Monitor Panel')).toBeInTheDocument()
    expect(screen.getByText('Dismiss Delay')).toBeInTheDocument()
    expect(screen.getByText('Yield Topmost on Full Screen')).toBeInTheDocument()
    // 4. Ranking tab
    fireEvent.click(screen.getByRole('button', { name: /Ranking/i }))
    expect(screen.getByText('Top N Applications')).toBeInTheDocument()
    expect(screen.getByText('Ranking Sliding Window')).toBeInTheDocument()
    expect(screen.getByText('Display Refresh Interval')).toBeInTheDocument()

    // 5. Collector tab
    fireEvent.click(screen.getByRole('button', { name: /Collector/i }))
    expect(screen.getByText('Collector Process Status')).toBeInTheDocument()
    expect(screen.getByText('Process ID (PID)')).toBeInTheDocument()
    expect(screen.getByText('ETW Session Health')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Restart Collector/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Retry Connection/i })).toBeInTheDocument()

    // 6. Diagnostics tab
    fireEvent.click(screen.getByRole('button', { name: /Diagnostics/i }))
    expect(screen.getByText('Diagnostics Log Level')).toBeInTheDocument()
    expect(screen.getByText('WebView2 Evergreen Runtime')).toBeInTheDocument()
  })

  it('triggers updateSetting on control changes', () => {
    const bridge = new MockBridge(false)
    const spySend = vi.spyOn(bridge, 'sendCommand')
    renderWithProviders(<SettingsCenter />, bridge)

    // Toggle launch at startup
    const startupToggle = screen.getByRole('switch', { name: /Launch at Windows startup/i })
    fireEvent.click(startupToggle)
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'startup.ui_at_login',
      value: true,
    })

    // Switch to capsule tab and change width
    fireEvent.click(screen.getByRole('button', { name: /Dock Capsule/i }))
    const widthSlider = screen.getByRole('slider', { name: 'Capsule Width' })
    fireEvent.change(widthSlider, { target: { value: '14' } })
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'floating.capsule_width_dip',
      value: 14,
    })
  })

  it('triggers collector restart and retry commands', () => {
    const bridge = new MockBridge(false)
    const spySend = vi.spyOn(bridge, 'sendCommand')
    renderWithProviders(<SettingsCenter initialTab="collector" />, bridge)

    fireEvent.click(screen.getByRole('button', { name: /Restart Collector/i }))
    expect(spySend).toHaveBeenCalledWith('restartCollector')

    fireEvent.click(screen.getByRole('button', { name: /Retry Connection/i }))
    expect(spySend).toHaveBeenCalledWith('retryCollector')
  })

  it('handles close button navigation', () => {
    const bridge = new MockBridge(false)
    const handleBack = vi.fn()
    renderWithProviders(<SettingsCenter onBack={handleBack} />, bridge)

    const closeBtn = screen.getByRole('button', { name: 'Close' })
    fireEvent.click(closeBtn)
    expect(handleBack).toHaveBeenCalledTimes(1)
  })
})
