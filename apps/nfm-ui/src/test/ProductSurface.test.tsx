import { act, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { ProductSurface } from '../app/App'
import { BridgeProvider } from '../bridge/BridgeContext'
import { MockBridge } from '../bridge/nativeBridge'
import { I18nProvider } from '../shared/i18n/I18nContext'
import { ThemeProvider } from '../shared/theme/ThemeContext'

function renderSurface(bridge: MockBridge) {
  return render(
    <BridgeProvider bridge={bridge}>
      <ThemeProvider>
        <I18nProvider>
          <ProductSurface />
        </I18nProvider>
      </ThemeProvider>
    </BridgeProvider>,
  )
}

describe('ProductSurface', () => {
  it('renders the surface selected by the native host', () => {
    const bridge = new MockBridge(false)
    renderSurface(bridge)
    expect(screen.getByRole('button', { name: 'Network Flow Monitor' })).toBeInTheDocument()

    act(() => bridge.emit('surfaceChanged', { surface: 'panel', pinned: false, dockSide: 'right' }))
    expect(screen.getByRole('region', { name: 'Monitor Panel' })).toBeInTheDocument()

    act(() => bridge.emit('surfaceChanged', { surface: 'settings', pinned: false, dockSide: 'right' }))
    expect(screen.getAllByText('General').length).toBeGreaterThan(0)
  })

  it('morphs scale and opacity between capsule and panel', () => {
    const bridge = new MockBridge(false)
    renderSurface(bridge)

    act(() => bridge.emit('surfaceChanged', { surface: 'panel', pinned: false, dockSide: 'right' }))
    const expandedPanel = screen.getByRole('region', { name: 'Monitor Panel' })
    expect(expandedPanel.style.transform).toBe('scale(1.0000, 1.0000)')
    expect(expandedPanel.style.opacity).toBe('1')

    act(() => bridge.emit('surfaceChanged', { surface: 'capsule', pinned: false, dockSide: 'right' }))
    const collapsedPanel = screen.getByRole('region', { name: 'Monitor Panel', hidden: true })
    expect(collapsedPanel.style.transform).not.toBe('scale(1.0000, 1.0000)')
    expect(collapsedPanel.style.opacity).toBe('0')
    expect(screen.getByRole('button', { name: 'Network Flow Monitor' })).toBeInTheDocument()
  })
})
