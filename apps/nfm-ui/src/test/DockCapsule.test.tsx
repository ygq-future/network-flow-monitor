import { act, fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { BridgeProvider } from '../bridge/BridgeContext'
import { MockBridge } from '../bridge/nativeBridge'
import { DockCapsule } from '../features/capsule/DockCapsule'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { ThemeProvider } from '../shared/theme/ThemeContext'

function renderCapsule(bridge: MockBridge) {
  return render(
    <BridgeProvider bridge={bridge}>
      <ThemeProvider>
        <I18nProvider>
          <DockCapsule />
        </I18nProvider>
      </ThemeProvider>
    </BridgeProvider>,
  )
}

describe('DockCapsule', () => {
  it('renders the configured capsule visual inside the native hit surface', () => {
    renderCapsule(new MockBridge(false))
    const visual = screen.getByTestId('capsule-visual')
    expect(visual).toHaveStyle({ width: '10px', height: '64px', borderRadius: '5px' })
    expect(visual).toHaveClass('p-0')
    expect(screen.queryByText('Download')).not.toBeInTheDocument()
  })
  it('requests the native coordinator to reveal the monitor', () => {
    const bridge = new MockBridge(false)
    const send = vi.spyOn(bridge, 'sendCommand')
    renderCapsule(bridge)
    fireEvent.keyDown(screen.getByRole('button', { name: 'Flow Monitor' }), { key: 'Enter' })
    expect(send).toHaveBeenCalledWith('showMonitor')
  })

  it('delegates the retained context menu to the native host', () => {
    const bridge = new MockBridge(false)
    const send = vi.spyOn(bridge, 'sendCommand')
    renderCapsule(bridge)
    fireEvent.contextMenu(screen.getByRole('button', { name: 'Flow Monitor' }), {
      screenX: 200,
      screenY: 300,
    })
    expect(send).toHaveBeenCalledWith('openContextMenu', { x: 200, y: 300 })
  })

  it('clears download and upload color fills immediately when collector disconnects or ETW becomes inactive', () => {
    const bridge = new MockBridge(false)
    renderCapsule(bridge)

    act(() => {
      bridge.emit('collectorStatus', {
        monotonicMs: 1000,
        etwSessionActive: true,
        droppedPackets: 0,
        activePids: 10,
        state: 'Running',
      })
      bridge.emit('trafficTick', {
        seq: 1,
        monotonicMs: 1000,
        downloadBytesPerSec: 2 * 1024 * 1024,
        uploadBytesPerSec: 1024 * 1024,
      })
    })

    const downloadFill = screen.getByTestId('download-fill')
    const uploadFill = screen.getByTestId('upload-fill')
    expect(downloadFill.style.height).not.toBe('0%')
    expect(uploadFill.style.height).not.toBe('0%')

    act(() => {
      bridge.emit('collectorStatus', {
        monotonicMs: 2000,
        etwSessionActive: false,
        droppedPackets: 0,
        activePids: 0,
        state: 'Running',
      })
    })

    expect(downloadFill.style.height).toBe('0%')
    expect(uploadFill.style.height).toBe('0%')
  })

  it('dampens a single-tick burst spike and decays gradually across subsequent zero ticks', () => {
    const bridge = new MockBridge(false)
    renderCapsule(bridge)

    act(() => {
      bridge.emit('collectorStatus', {
        monotonicMs: 1000,
        etwSessionActive: true,
        droppedPackets: 0,
        activePids: 8,
        state: 'Running',
      })
      bridge.emit('trafficTick', {
        seq: 1,
        monotonicMs: 1000,
        downloadBytesPerSec: 2 * 1024 * 1024,
        uploadBytesPerSec: 0,
      })
    })

    const downloadFill = screen.getByTestId('download-fill')
    const spikeHeight = parseFloat(downloadFill.style.height)
    expect(spikeHeight).toBeGreaterThan(0)
    expect(spikeHeight).toBeLessThan(25)

    act(() => {
      bridge.emit('trafficTick', {
        seq: 2,
        monotonicMs: 1250,
        downloadBytesPerSec: 0,
        uploadBytesPerSec: 0,
      })
    })

    const firstDecayHeight = parseFloat(downloadFill.style.height)
    expect(firstDecayHeight).toBeGreaterThan(0)
    expect(firstDecayHeight).toBeLessThan(spikeHeight)

    act(() => {
      bridge.emit('trafficTick', {
        seq: 3,
        monotonicMs: 1500,
        downloadBytesPerSec: 0,
        uploadBytesPerSec: 0,
      })
    })

    const secondDecayHeight = parseFloat(downloadFill.style.height)
    expect(secondDecayHeight).toBeGreaterThan(0)
    expect(secondDecayHeight).toBeLessThan(firstDecayHeight)
  })
})
