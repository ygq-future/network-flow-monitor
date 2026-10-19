import { fireEvent, render, screen } from '@testing-library/react'
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
})
