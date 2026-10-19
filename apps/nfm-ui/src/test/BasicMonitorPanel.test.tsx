import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { BasicMonitorPanel } from '../features/capsule/BasicMonitorPanel'
import { BridgeProvider } from '../bridge/BridgeContext'
import { MockBridge } from '../bridge/nativeBridge'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { ThemeProvider } from '../shared/theme/ThemeContext'
import React from 'react'

function renderWithProviders(ui: React.ReactElement) {
  const bridge = new MockBridge(false)
  return render(
    <BridgeProvider bridge={bridge}>
      <ThemeProvider>
        <I18nProvider>{ui}</I18nProvider>
      </ThemeProvider>
    </BridgeProvider>,
  )
}

describe('BasicMonitorPanel', () => {
  const mockTick = {
    seq: 1,
    monotonicMs: 1000,
    downloadBytesPerSec: 5 * 1024 * 1024, // 5 MB/s
    uploadBytesPerSec: 1.5 * 1024 * 1024, // 1.5 MB/s
  }

  const mockStatus = {
    monotonicMs: 1000,
    etwSessionActive: true,
    droppedPackets: 0,
    activePids: 42,
    state: 'Running' as const,
  }

  it('renders compact panel with Total Rate row and download/upload speeds', () => {
    renderWithProviders(
      <BasicMonitorPanel tick={mockTick} status={mockStatus} isPinned={false} onTogglePin={vi.fn()} />,
    )

    expect(screen.getByRole('region', { name: /Monitor Panel/i })).toBeInTheDocument()
    expect(screen.getByTestId('total-download-rate')).toHaveTextContent('5.0 MB/s')
    expect(screen.getByTestId('total-upload-rate')).toHaveTextContent('1.5 MB/s')
  })

  it('renders Top N application ranking rows when rankingSnapshot is provided', () => {
    const mockRanking = {
      seq: 2,
      monotonicMs: 1000,
      status: 'Ready',
      totalDownloadBytesPerSec: 3 * 1024 * 1024,
      totalUploadBytesPerSec: 1 * 1024 * 1024,
      applications: [
        {
          id: 'edge',
          executablePath: 'C:\\msedge.exe',
          displayName: 'Microsoft Edge',
          iconKey: 'edge',
          downloadBytesPerSec: 2 * 1024 * 1024,
          uploadBytesPerSec: 512 * 1024,
        },
      ],
    }
    renderWithProviders(
      <BasicMonitorPanel
        tick={mockTick}
        ranking={mockRanking}
        status={mockStatus}
        isPinned={false}
        onTogglePin={vi.fn()}
      />,
    )
    expect(screen.getByTestId('total-download-rate')).toHaveTextContent('3.0 MB/s')
    expect(screen.getByTestId('total-upload-rate')).toHaveTextContent('1.0 MB/s')
    expect(screen.getByText('Microsoft Edge')).toBeInTheDocument()
    expect(screen.getByText('2.0 MB/s')).toBeInTheDocument()
    expect(screen.getByTestId('app-download-bar')).toHaveStyle({ width: '80.00%' })
    expect(screen.getByTestId('app-upload-bar')).toHaveStyle({ width: '20.00%' })
  })

  it('handles Pin toggle button click', () => {
    const handleTogglePin = vi.fn()
    renderWithProviders(
      <BasicMonitorPanel tick={mockTick} status={mockStatus} isPinned={false} onTogglePin={handleTogglePin} />,
    )

    const pinBtn = screen.getByRole('button', { name: 'Pin' })
    fireEvent.click(pinBtn)
    expect(handleTogglePin).toHaveBeenCalledTimes(1)
  })

  it('handles Settings navigation callback', () => {
    const handleOpenSettings = vi.fn()

    renderWithProviders(
      <BasicMonitorPanel
        tick={mockTick}
        status={mockStatus}
        isPinned={false}
        onTogglePin={vi.fn()}
        onOpenSettings={handleOpenSettings}
      />,
    )

    const settingsBtn = screen.getByRole('button', { name: 'Settings' })
    fireEvent.click(settingsBtn)
    expect(handleOpenSettings).toHaveBeenCalledTimes(1)
  })

  it('handles context menu event', () => {
    const handleContextMenu = vi.fn()
    renderWithProviders(
      <BasicMonitorPanel
        tick={mockTick}
        status={mockStatus}
        isPinned={false}
        onTogglePin={vi.fn()}
        onContextMenu={handleContextMenu}
      />,
    )

    const panel = screen.getByRole('region', { name: /Monitor Panel/i })
    fireEvent.contextMenu(panel)
    expect(handleContextMenu).toHaveBeenCalledTimes(1)
  })

  it('applies explicit height and fixed 340px width for smooth height transitions without width jitter', () => {
    const { rerender } = render(
      <BridgeProvider bridge={new MockBridge(false)}>
        <ThemeProvider>
          <I18nProvider>
            <BasicMonitorPanel
              tick={{
                seq: 1,
                monotonicMs: 1000,
                downloadBytesPerSec: 1022.3 * 1024,
                uploadBytesPerSec: 1022.3 * 1024,
              }}
              status={mockStatus}
              isPinned={false}
              onTogglePin={vi.fn()}
            />
          </I18nProvider>
        </ThemeProvider>
      </BridgeProvider>,
    )

    const panel = screen.getByRole('region', { name: /Monitor Panel/i })
    expect(panel).toHaveClass('w-[340px]', 'min-w-[340px]', 'max-w-[340px]')
    expect(panel.style.height).toBe('84px')
    expect(screen.getByTestId('total-download-rate')).toHaveTextContent('1.0 MB/s')
    expect(screen.getByTestId('total-upload-rate')).toHaveTextContent('1.0 MB/s')

    rerender(
      <BridgeProvider bridge={new MockBridge(false)}>
        <ThemeProvider>
          <I18nProvider>
            <BasicMonitorPanel
              tick={mockTick}
              ranking={{
                seq: 2,
                monotonicMs: 2000,
                status: 'Ready',
                totalDownloadBytesPerSec: 512 * 1024,
                totalUploadBytesPerSec: 128 * 1024,
                applications: [
                  {
                    id: 'app1',
                    executablePath: 'C:\\a.exe',
                    displayName: 'App 1',
                    iconKey: 'a',
                    downloadBytesPerSec: 256 * 1024,
                    uploadBytesPerSec: 64 * 1024,
                  },
                  {
                    id: 'app2',
                    executablePath: 'C:\\b.exe',
                    displayName: 'App 2',
                    iconKey: 'b',
                    downloadBytesPerSec: 256 * 1024,
                    uploadBytesPerSec: 64 * 1024,
                  },
                  {
                    id: 'app3',
                    executablePath: 'C:\\c.exe',
                    displayName: 'App 3',
                    iconKey: 'c',
                    downloadBytesPerSec: 256 * 1024,
                    uploadBytesPerSec: 64 * 1024,
                  },
                ],
              }}
              status={mockStatus}
              isPinned={false}
              onTogglePin={vi.fn()}
            />
          </I18nProvider>
        </ThemeProvider>
      </BridgeProvider>,
    )

    expect(panel.style.height).toBe('136px')
    expect(panel.style.transition).toContain('height 240ms cubic-bezier(0.22, 1, 0.36, 1)')
  })
})
