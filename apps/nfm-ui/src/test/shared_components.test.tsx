import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { Button } from '../shared/components/Button'
import { IconButton } from '../shared/components/IconButton'
import { Toggle } from '../shared/components/Toggle'
import { Slider } from '../shared/components/Slider'
import { Checkbox } from '../shared/components/Checkbox'
import { SettingsSection } from '../shared/components/SettingsSection'
import { SettingsRow } from '../shared/components/SettingsRow'
import { Select } from '../shared/components/Select'

describe('Shared Components', () => {
  it('Button handles clicks and disabled state', () => {
    const handleClick = vi.fn()
    const { rerender } = render(<Button onClick={handleClick}>Click Me</Button>)

    fireEvent.click(screen.getByText('Click Me'))
    expect(handleClick).toHaveBeenCalledTimes(1)

    rerender(
      <Button onClick={handleClick} disabled>
        Disabled
      </Button>,
    )
    fireEvent.click(screen.getByText('Disabled'))
    expect(handleClick).toHaveBeenCalledTimes(1)
  })

  it('IconButton renders with aria-label', () => {
    const handleClick = vi.fn()
    render(
      <IconButton aria-label="Close modal" onClick={handleClick}>
        <span>X</span>
      </IconButton>,
    )

    const btn = screen.getByRole('button', { name: 'Close modal' })
    expect(btn).toBeInTheDocument()
    fireEvent.click(btn)
    expect(handleClick).toHaveBeenCalledTimes(1)
  })

  it('Toggle renders and switches state', () => {
    const handleChange = vi.fn()
    const { rerender } = render(<Toggle checked={false} onChange={handleChange} ariaLabel="Auto-hide" />)

    const switchBtn = screen.getByRole('switch', { name: 'Auto-hide' })
    expect(switchBtn).toHaveAttribute('aria-checked', 'false')

    fireEvent.click(switchBtn)
    expect(handleChange).toHaveBeenCalledWith(true)

    rerender(<Toggle checked={true} onChange={handleChange} ariaLabel="Auto-hide" />)
    expect(switchBtn).toHaveAttribute('aria-checked', 'true')
  })

  it('Slider handles value changes within range', () => {
    const handleChange = vi.fn()
    render(<Slider value={10} min={8} max={24} unit="DIP" onChange={handleChange} ariaLabel="Capsule Width" />)

    expect(screen.getByText('10 DIP')).toBeInTheDocument()
    const sliderInput = screen.getByRole('slider', { name: 'Capsule Width' })
    fireEvent.change(sliderInput, { target: { value: '16' } })
    expect(handleChange).toHaveBeenCalledWith(16)
  })

  it('Checkbox renders and toggles', () => {
    const handleChange = vi.fn()
    render(<Checkbox checked={false} onChange={handleChange} label="Startup at Login" description="Launch app" />)

    expect(screen.getByText('Startup at Login')).toBeInTheDocument()
    expect(screen.getByText('Launch app')).toBeInTheDocument()

    const checkbox = screen.getByRole('checkbox', { name: 'Startup at Login' })
    fireEvent.click(checkbox)
    expect(handleChange).toHaveBeenCalledWith(true)
  })

  it('Select triggers onChange on option selection', () => {
    const handleChange = vi.fn()
    const options = [
      { value: 'auto', label: 'Auto' },
      { value: 'bytes', label: 'Bytes' },
    ]
    render(<Select value="auto" onChange={handleChange} options={options} />)

    const selectEl = screen.getByRole('button', { name: 'Auto' })
    fireEvent.click(selectEl)
    fireEvent.click(screen.getByRole('option', { name: 'Bytes' }))
    expect(handleChange).toHaveBeenCalledWith('bytes')
  })

  it('SettingsSection and SettingsRow render title, description, and control', () => {
    render(
      <SettingsSection title="General" description="Basic preferences">
        <SettingsRow label="Speed Unit" description="Unit format" control={<button>Control</button>} />
      </SettingsSection>,
    )

    expect(screen.getByText('General')).toBeInTheDocument()
    expect(screen.getByText('Basic preferences')).toBeInTheDocument()
    expect(screen.getByText('Speed Unit')).toBeInTheDocument()
    expect(screen.getByText('Unit format')).toBeInTheDocument()
    expect(screen.getByText('Control')).toBeInTheDocument()
  })
})
