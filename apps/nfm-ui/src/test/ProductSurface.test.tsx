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
})
