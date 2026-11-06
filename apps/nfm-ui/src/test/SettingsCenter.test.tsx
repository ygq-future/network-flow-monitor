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
  it('renders 4 tabs including Taskbar Monitor and Traffic History and default general section', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    expect(screen.getByText('Settings')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /General/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /^Capsule$/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Taskbar Monitor/i })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Traffic History/i })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: /Collector & Logs/i })).not.toBeInTheDocument()
    expect(screen.getByText('Launch at Windows startup')).toBeInTheDocument()
    expect(screen.queryByText('Show Taskbar Speed')).not.toBeInTheDocument()
    expect(screen.queryByText('Speed Unit')).not.toBeInTheDocument()
    expect(screen.getByText('Application Theme')).toBeInTheDocument()
    expect(screen.getByText('Language')).toBeInTheDocument()
    expect(screen.getByText('Top N Applications')).toBeInTheDocument()
    expect(screen.queryByText('Ranking Sliding Window')).not.toBeInTheDocument()
    expect(screen.queryByText('Display Refresh Interval')).not.toBeInTheDocument()
    expect(screen.getByText('Version & Online Update')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Check for Updates/i })).toBeInTheDocument()
  })

  it('switches tabs to Capsule and Traffic History', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter />, bridge)

    // 1. Capsule tab
    fireEvent.click(screen.getByRole('button', { name: /^Capsule$/i }))
    expect(screen.getByText('Capsule Width')).toBeInTheDocument()
    expect(screen.getByText('Capsule Height')).toBeInTheDocument()
    expect(screen.getByText('End Rounding')).toBeInTheDocument()
    expect(screen.getByText('Hover Expand Delay')).toBeInTheDocument()
    expect(screen.getByText('Dismiss Delay')).toBeInTheDocument()
    expect(screen.getByText('Capsule Rate Colors')).toBeInTheDocument()
    expect(screen.getByText('Lock & Click-Through on Full Screen')).toBeInTheDocument()
    expect(screen.getByText('Hide Capsule on Full Screen')).toBeInTheDocument()
    // 2. Traffic History tab
    fireEvent.click(screen.getByRole('button', { name: /Traffic History/i }))
    expect(screen.getByText('Period')).toBeInTheDocument()
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

    // Switch to Taskbar Monitor tab and toggle taskbar speed & hardware metrics
    fireEvent.click(screen.getByRole('button', { name: /Taskbar Monitor/i }))
    const taskbarSpeedToggle = screen.getByRole('switch', { name: /Show Taskbar Speed/i })
    fireEvent.click(taskbarSpeedToggle)
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'display.show_taskbar_speed',
      value: true,
    })
    const cpuUsageToggle = screen.getByRole('switch', { name: /Show CPU Usage/i })
    fireEvent.click(cpuUsageToggle)
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'display.show_taskbar_cpu_usage',
      value: true,
    })
    const gpuMemToggle = screen.getByRole('switch', { name: /Show GPU Memory/i })
    fireEvent.click(gpuMemToggle)
    expect(spySend).toHaveBeenCalledWith('updateSetting', {
      key: 'display.show_taskbar_gpu_memory',
      value: true,
    })
    // Switch to capsule tab and change width
    fireEvent.click(screen.getByRole('button', { name: /^Capsule$/i }))
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

  it('normalizes legacy collector tab to general', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter initialTab="collector" />, bridge)
    expect(screen.getByText('Launch at Windows startup')).toBeInTheDocument()
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
  it('manages excluded programs via manual input and connected apps fuzzy search', () => {
    const bridge = new MockBridge(false)
    const spy = vi.spyOn(bridge, 'sendCommand')
    renderWithProviders(<SettingsCenter />, bridge)

    const manageBtn = screen.getByRole('button', { name: /Manage Exclusions/i })
    fireEvent.click(manageBtn)

    expect(spy).toHaveBeenCalledWith('requestConnectedApplications')

    act(() => {
      bridge.emit('connectedApplications', {
        applications: [
          {
            id: 'app-mihomo',
            executablePath: 'C:\\Proxy\\mihomo-multi.exe',
            executableName: 'mihomo-multi.exe',
            displayName: 'mihomo-multi.exe',
            iconKey: 'mihomo',
            totalDownloadBytes: 50000,
            totalUploadBytes: 50000,
          },
          {
            id: 'app-edge',
            executablePath: 'C:\\Edge\\msedge.exe',
            executableName: 'msedge.exe',
            displayName: 'Microsoft Edge',
            iconKey: 'msedge',
            totalDownloadBytes: 25000,
            totalUploadBytes: 12000,
          },
        ],
      })
    })

    expect(screen.getByText('mihomo-multi.exe')).toBeInTheDocument()

    const addBtn = screen.getByRole('button', { name: /^Add$/i })
    expect(addBtn).toBeDisabled()

    // Clicking a connected app row selects it without immediately adding
    fireEvent.click(screen.getByText('mihomo-multi.exe'))
    expect(spy).not.toHaveBeenCalledWith('updateSetting', expect.anything())
    expect(addBtn).not.toBeDisabled()

    // Unified input filters connected apps and clears selection while typing
    const searchInput = screen.getByPlaceholderText(/Search connected apps or enter process name/i)
    fireEvent.change(searchInput, { target: { value: 'mihomo' } })
    expect(screen.getByText('mihomo-multi.exe')).toBeInTheDocument()
    expect(screen.queryByText('Microsoft Edge')).not.toBeInTheDocument()
    fireEvent.change(searchInput, { target: { value: '' } })
    expect(addBtn).toBeDisabled()

    // Select connected app row, then click Add button to commit
    fireEvent.click(screen.getByText('mihomo-multi.exe'))
    expect(addBtn).not.toBeDisabled()
    fireEvent.click(addBtn)

    expect(spy).toHaveBeenCalledWith('updateSetting', {
      key: 'ranking.excluded_programs',
      value: ['mihomo-multi.exe'],
    })
    // Refresh button triggers requestConnectedApplications
    const refreshBtn = screen.getByRole('button', { name: /Refresh/i })
    fireEvent.click(refreshBtn)
    expect(spy).toHaveBeenCalledWith('requestConnectedApplications')

    // Switching tabs or re-opening settings collapses the exclusion panel by default
    fireEvent.click(screen.getByRole('button', { name: /^Capsule$/i }))
    fireEvent.click(screen.getByRole('button', { name: /^General$/i }))
    expect(screen.queryByPlaceholderText(/Search connected apps or enter process name/i)).not.toBeInTheDocument()

    fireEvent.click(screen.getByRole('button', { name: /Manage Exclusions/i }))
    expect(screen.getByPlaceholderText(/Search connected apps or enter process name/i)).toBeInTheDocument()
    act(() => {
      bridge.emit('surfaceChanged', {
        surface: 'settings',
        pinned: false,
        dockSide: 'right',
        autoHidden: false,
        route: 'general',
      })
    })
    expect(screen.queryByPlaceholderText(/Search connected apps or enter process name/i)).not.toBeInTheDocument()
  })

  it('supports multi-level progressive traffic history drill-down, period granularity, search, sorting and scale toggle', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<SettingsCenter initialTab="history" />, bridge)

    act(() => {
      bridge.emit('trafficHistoryUpdated', {
        schemaVersion: 1,
        days: [
          {
            date: '2026-10-20',
            totalDownloadBytes: 2097152,
            totalUploadBytes: 1048576,
            applications: [
              {
                appId: 'executable:c:\\edge\\msedge.exe',
                executablePath: 'C:\\Edge\\msedge.exe',
                executableName: 'msedge.exe',
                displayName: 'Microsoft Edge',
                iconKey: '',
                downloadBytes: 1572864,
                uploadBytes: 524288,
              },
              {
                appId: 'executable:c:\\code\\code.exe',
                executablePath: 'C:\\Code\\Code.exe',
                executableName: 'Code.exe',
                displayName: 'Visual Studio Code',
                iconKey: '',
                downloadBytes: 524288,
                uploadBytes: 524288,
              },
            ],
          },
          {
            date: '2026-09-15',
            totalDownloadBytes: 1048576,
            totalUploadBytes: 524288,
            applications: [
              {
                appId: 'executable:c:\\edge\\msedge.exe',
                executablePath: 'C:\\Edge\\msedge.exe',
                executableName: 'msedge.exe',
                displayName: 'Microsoft Edge',
                iconKey: '',
                downloadBytes: 1048576,
                uploadBytes: 524288,
              },
            ],
          },
        ],
      })
    })

    // Default is By Time + Day granularity: shows 26/10/20 and 26/09/15
    expect(screen.getByText('26/10/20')).toBeInTheDocument()
    expect(screen.getByText('26/09/15')).toBeInTheDocument()

    // Switch granularity to Month -> shows 26/10 and 26/09
    fireEvent.click(screen.getByRole('button', { name: 'Mo' }))
    expect(screen.getByText('26/10')).toBeInTheDocument()
    expect(screen.getByText('26/09')).toBeInTheDocument()

    // Switch granularity to Quarter -> shows 26 Q4 and 26 Q3
    fireEvent.click(screen.getByRole('button', { name: 'Qtr' }))
    expect(screen.getByText('26 Q4')).toBeInTheDocument()
    expect(screen.getByText('26 Q3')).toBeInTheDocument()

    // Switch granularity to Year -> shows 26
    fireEvent.click(screen.getByRole('button', { name: 'Yr' }))
    expect(screen.getByText('26')).toBeInTheDocument()

    // Switch back to Day and drill down into 26/10/20 -> shows Microsoft Edge and Visual Studio Code
    fireEvent.click(screen.getByRole('button', { name: 'Day' }))
    fireEvent.click(screen.getByText('26/10/20'))
    expect(screen.getByText('Microsoft Edge')).toBeInTheDocument()
    expect(screen.getByText('Visual Studio Code')).toBeInTheDocument()

    // Drill down further into Microsoft Edge -> shows its period breakdown across days
    fireEvent.click(screen.getByText('Microsoft Edge'))
    expect(screen.getAllByText('26/10/20').length).toBeGreaterThanOrEqual(2)
    expect(screen.getByText('26/09/15')).toBeInTheDocument()
    // Switch root dimension to By Application
    fireEvent.click(screen.getByRole('button', { name: /By App/i }))
    expect(screen.getByText('Microsoft Edge')).toBeInTheDocument()
    expect(screen.getByText('Visual Studio Code')).toBeInTheDocument()

    // Fuzzy search filter
    const filterInput = screen.getByPlaceholderText(/Filter by date, app, or executable/i)
    fireEvent.change(filterInput, { target: { value: 'studio' } })
    expect(screen.getByText('Visual Studio Code')).toBeInTheDocument()
    expect(screen.queryByText('Microsoft Edge')).not.toBeInTheDocument()

    // Clear search and test scale toggle (Log <-> Linear) and column sorting
    fireEvent.change(filterInput, { target: { value: '' } })
    fireEvent.click(screen.getByRole('button', { name: 'Lin' }))
    fireEvent.click(screen.getByRole('button', { name: 'Log' }))
    fireEvent.click(screen.getByRole('button', { name: /↓ Download/i }))
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
