import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent, act } from '@testing-library/react'
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
  it('renders 3 tabs and default general section', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    expect(screen.getByText('Settings')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /General/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Dock Capsule/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Collector & Logs/i })).toBeInTheDocument()

    // General tab content
    expect(screen.getByText('Launch at Windows startup')).toBeInTheDocument()
    expect(screen.getByText('Speed Unit')).toBeInTheDocument()
    expect(screen.getByText('Application Theme')).toBeInTheDocument()
    expect(screen.getByText('Language')).toBeInTheDocument()
    expect(screen.getByText('Top N Applications')).toBeInTheDocument()
    expect(screen.getByText('Ranking Sliding Window')).toBeInTheDocument()
    expect(screen.getByText('Display Refresh Interval')).toBeInTheDocument()
    expect(screen.getByText('Version & Online Update')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Check for Updates/i })).toBeInTheDocument()
  })

  it('switches tabs to Capsule and Collector & Logs', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    // 1. Capsule tab
    fireEvent.click(screen.getByRole('button', { name: /Dock Capsule/i }))
    expect(screen.getByText('Capsule Width')).toBeInTheDocument()
    expect(screen.getByText('Capsule Height')).toBeInTheDocument()
    expect(screen.getByText('End Rounding')).toBeInTheDocument()
    expect(screen.getByText('Hover Expand Delay')).toBeInTheDocument()
    expect(screen.getByText('Dismiss Delay')).toBeInTheDocument()
    expect(screen.getByText('Capsule Rate Colors')).toBeInTheDocument()
    expect(screen.getByText('Lock & Click-Through on Full Screen')).toBeInTheDocument()
    expect(screen.getByText('Hide Capsule on Full Screen')).toBeInTheDocument()
    // 2. Collector & Logs tab
    fireEvent.click(screen.getByRole('button', { name: /Collector & Logs/i }))
    expect(screen.getByText('Collector Process Status')).toBeInTheDocument()
    expect(screen.getByText('Process ID (PID)')).toBeInTheDocument()
    expect(screen.getByText('ETW Session Health')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Restart Collector/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Retry Connection/i })).toBeInTheDocument()
    expect(screen.getByText('Diagnostics Log Level')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Open Log Folder/i })).toBeInTheDocument()
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

    // Toggle fullscreen hide capsule
    const hideFullscreenToggle = screen.getByRole('switch', { name: /Hide Capsule on Full Screen/i })
    fireEvent.click(hideFullscreenToggle)
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'floating.hide_capsule_when_fullscreen',
      value: true,
    })
  })

  it('triggers collector restart, retry, and openLogDirectory commands', () => {
    const bridge = new MockBridge(false)
    const spySend = vi.spyOn(bridge, 'sendCommand')
    renderWithProviders(<SettingsCenter initialTab="collector" />, bridge)

    fireEvent.click(screen.getByRole('button', { name: /Restart Collector/i }))
    expect(spySend).toHaveBeenCalledWith('restartCollector')

    fireEvent.click(screen.getByRole('button', { name: /Retry Connection/i }))
    expect(spySend).toHaveBeenCalledWith('retryCollector')

    fireEvent.click(screen.getByRole('button', { name: /Open Log Folder/i }))
    expect(spySend).toHaveBeenCalledWith('openLogDirectory')
  })
  it('handles checkForUpdate and downloadAndApplyUpdate in General tab', () => {
    const bridge = new MockBridge(false)
    const spySend = vi.spyOn(bridge, 'sendCommand')
    renderWithProviders(<SettingsCenter initialTab="general" />, bridge)

    const checkBtn = screen.getByRole('button', { name: /Check for Updates/i })
    fireEvent.click(checkBtn)
    expect(spySend).toHaveBeenCalledWith('checkForUpdate')

    act(() => {
      bridge.emit('updateStatus', {
        state: 'UpdateAvailable',
        currentVersion: '0.1.0',
        latestVersion: '0.2.0',
        downloadProgressPercent: 0,
        errorMessage: '',
      })
    })

    expect(screen.getByText(/Update Available: v0.2.0/i)).toBeInTheDocument()
    const applyBtn = screen.getByRole('button', { name: /Update & Restart/i })
    fireEvent.click(applyBtn)
    expect(spySend).toHaveBeenCalledWith('downloadAndApplyUpdate')
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
