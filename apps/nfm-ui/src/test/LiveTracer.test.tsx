import { describe, it, expect } from 'vitest'
import { render, screen, act } from '@testing-library/react'
import { LiveTracer } from '../features/tracer/LiveTracer'
import { BridgeProvider } from '../bridge/BridgeContext'
import { ThemeProvider } from '../shared/theme/ThemeContext'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { MockBridge } from '../bridge/nativeBridge'

describe('LiveTracer component', () => {
  it('renders throughput rates and updates on traffic tick', () => {
    const mockBridge = new MockBridge(false)

    render(
      <BridgeProvider bridge={mockBridge}>
        <ThemeProvider>
          <I18nProvider initialLocale="English">
            <LiveTracer />
          </I18nProvider>
        </ThemeProvider>
      </BridgeProvider>,
    )

    expect(screen.getByText('Live Flow Tracer')).toBeInTheDocument()
    expect(screen.getByText('Download')).toBeInTheDocument()
    expect(screen.getByText('Upload')).toBeInTheDocument()

    // Emit a mock tick
    act(() => {
      mockBridge.emit('trafficTick', {
        seq: 1,
        monotonicMs: 1000,
        downloadBytesPerSec: 2048 * 1024,
        uploadBytesPerSec: 512 * 1024,
      })
    })

    expect(screen.getByText('2.0')).toBeInTheDocument()
    expect(screen.getByText('MB/s')).toBeInTheDocument()
    expect(screen.getByText('512.0')).toBeInTheDocument()
    expect(screen.getByText('KB/s')).toBeInTheDocument()

    mockBridge.destroy()
  })
})
