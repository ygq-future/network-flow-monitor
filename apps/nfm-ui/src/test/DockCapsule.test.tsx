import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { DockCapsule } from '../features/capsule/DockCapsule'
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

describe('DockCapsule', () => {
  it('renders capsule and rates pill', () => {
    const bridge = new MockBridge(false)
    renderWithProviders(<DockCapsule />, bridge)

    const capsuleBtn = screen.getByRole('button', { name: /Network Flow Monitor/i })
    expect(capsuleBtn).toBeInTheDocument()
  })

  it('expands popover card on click', () => {
    const bridge = new MockBridge(false)
    const handleOpenSettings = vi.fn()
    renderWithProviders(<DockCapsule onOpenSettings={handleOpenSettings} />, bridge)

    const capsuleBtn = screen.getByRole('button', { name: /Network Flow Monitor/i })
    fireEvent.click(capsuleBtn)

    expect(screen.getByText('Download')).toBeInTheDocument()
    expect(screen.getByText('Upload')).toBeInTheDocument()
  })

  it('handles settings navigation from popover', () => {
    const bridge = new MockBridge(false)
    const handleOpenSettings = vi.fn()
    renderWithProviders(<DockCapsule onOpenSettings={handleOpenSettings} />, bridge)

    // Expand
    const capsuleBtn = screen.getByRole('button', { name: /Network Flow Monitor/i })
    fireEvent.click(capsuleBtn)

    const settingsBtns = screen.getAllByRole('button', { name: 'Settings' })
    fireEvent.click(settingsBtns[0])
    expect(handleOpenSettings).toHaveBeenCalledTimes(1)
  })
})
