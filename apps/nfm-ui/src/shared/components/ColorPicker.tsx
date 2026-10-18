import { useState, useRef, useEffect, useLayoutEffect, useCallback } from 'react'
import { createPortal } from 'react-dom'
import { Check } from 'lucide-react'

export interface ColorPickerProps {
  value: string
  onChange: (hex: string) => void
  label?: string
  prefix?: string
  ariaLabel?: string
  className?: string
}

interface PopoverGeometry {
  top: number
  left: number
  placement: 'bottom' | 'top'
}

interface HsvColor {
  h: number // 0 - 360
  s: number // 0 - 1
  v: number // 0 - 1
}

const QUICK_SWATCHES = [
  '#38bdf8',
  '#2dd4bf',
  '#10b981',
  '#84cc16',
  '#60a5fa',
  '#818cf8',
  '#a78bfa',
  '#e879f9',
  '#f472b6',
  '#fb7185',
  '#fb923c',
  '#facc15',
]

function normalizeHex(input: string): string | null {
  const clean = input.trim().replace(/^#/, '')
  if (/^[0-9a-fA-F]{6}$/.test(clean)) {
    return `#${clean.toLowerCase()}`
  }
  if (/^[0-9a-fA-F]{3}$/.test(clean)) {
    const r = clean[0]
    const g = clean[1]
    const b = clean[2]
    return `#${r}${r}${g}${g}${b}${b}`.toLowerCase()
  }
  return null
}

function hexToHsv(hex: string): HsvColor {
  const normalized = normalizeHex(hex) ?? '#38bdf8'
  const r = parseInt(normalized.slice(1, 3), 16) / 255
  const g = parseInt(normalized.slice(3, 5), 16) / 255
  const b = parseInt(normalized.slice(5, 7), 16) / 255

  const max = Math.max(r, g, b)
  const min = Math.min(r, g, b)
  const d = max - min

  let h = 0
  const s = max === 0 ? 0 : d / max
  const v = max

  if (max !== min) {
    switch (max) {
      case r:
        h = (g - b) / d + (g < b ? 6 : 0)
        break
      case g:
        h = (b - r) / d + 2
        break
      case b:
        h = (r - g) / d + 4
        break
    }
    h *= 60
  }

  return { h, s, v }
}

function hsvToHex({ h, s, v }: HsvColor): string {
  const c = v * s
  const hh = ((h % 360) + 360) % 360
  const x = c * (1 - Math.abs(((hh / 60) % 2) - 1))
  const m = v - c

  let rPrime = 0
  let gPrime = 0
  let bPrime = 0

  if (hh < 60) {
    rPrime = c
    gPrime = x
  } else if (hh < 120) {
    rPrime = x
    gPrime = c
  } else if (hh < 180) {
    gPrime = c
    bPrime = x
  } else if (hh < 240) {
    gPrime = x
    bPrime = c
  } else if (hh < 300) {
    rPrime = x
    bPrime = c
  } else {
    rPrime = c
    bPrime = x
  }

  const toHex = (n: number) =>
    Math.round(Math.min(255, Math.max(0, (n + m) * 255)))
      .toString(16)
      .padStart(2, '0')

  return `#${toHex(rPrime)}${toHex(gPrime)}${toHex(bPrime)}`
}

const POPOVER_WIDTH = 216
const POPOVER_ESTIMATED_HEIGHT = 236

export function ColorPicker({ value, onChange, label, prefix, ariaLabel, className = '' }: ColorPickerProps) {
  const normalizedValue = (normalizeHex(value) ?? '#38bdf8').toLowerCase()
  const [open, setOpen] = useState(false)
  const [entered, setEntered] = useState(false)
  const [hsv, setHsv] = useState<HsvColor>(() => hexToHsv(normalizedValue))
  const [hexDraft, setHexDraft] = useState(normalizedValue.slice(1).toUpperCase())
  const [geometry, setGeometry] = useState<PopoverGeometry>({
    top: 0,
    left: 0,
    placement: 'bottom',
  })

  const triggerRef = useRef<HTMLButtonElement>(null)
  const popoverRef = useRef<HTMLDivElement>(null)
  const svCanvasRef = useRef<HTMLDivElement>(null)
  const hueBarRef = useRef<HTMLDivElement>(null)
  const isDraggingSvRef = useRef(false)
  const isDraggingHueRef = useRef(false)

  useEffect(() => {
    if (!isDraggingSvRef.current && !isDraggingHueRef.current) {
      setHsv(hexToHsv(normalizedValue))
      setHexDraft(normalizedValue.slice(1).toUpperCase())
    }
  }, [normalizedValue])

  const updatePosition = useCallback(() => {
    const triggerEl = triggerRef.current
    if (!triggerEl) return
    const rect = triggerEl.getBoundingClientRect()
    const viewportWidth = window.innerWidth || 520
    const viewportHeight = window.innerHeight || 390
    const spaceBelow = viewportHeight - rect.bottom - 8
    const spaceAbove = rect.top - 8
    const placement: 'bottom' | 'top' =
      spaceBelow < POPOVER_ESTIMATED_HEIGHT && spaceAbove > spaceBelow ? 'top' : 'bottom'

    const idealLeft = rect.right - POPOVER_WIDTH
    const clampedLeft = Math.max(8, Math.min(idealLeft, viewportWidth - POPOVER_WIDTH - 8))

    setGeometry({
      top: placement === 'bottom' ? rect.bottom + 6 : rect.top - 6,
      left: clampedLeft,
      placement,
    })
  }, [])

  useLayoutEffect(() => {
    if (!open) {
      setEntered(false)
      return
    }
    updatePosition()
    const rafId = requestAnimationFrame(() => setEntered(true))
    return () => cancelAnimationFrame(rafId)
  }, [open, updatePosition])

  useEffect(() => {
    if (!open) return
    const handleClickOutside = (event: MouseEvent) => {
      const target = event.target as Node
      if (triggerRef.current?.contains(target) || popoverRef.current?.contains(target)) {
        return
      }
      setOpen(false)
    }
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpen(false)
      }
    }
    const handleViewportChange = () => {
      updatePosition()
    }
    document.addEventListener('mousedown', handleClickOutside)
    document.addEventListener('keydown', handleKeyDown)
    window.addEventListener('resize', handleViewportChange)
    window.addEventListener('scroll', handleViewportChange, true)
    return () => {
      document.removeEventListener('mousedown', handleClickOutside)
      document.removeEventListener('keydown', handleKeyDown)
      window.removeEventListener('resize', handleViewportChange)
      window.removeEventListener('scroll', handleViewportChange, true)
    }
  }, [open, updatePosition])

  const applyHsvUpdate = useCallback(
    (nextHsv: HsvColor) => {
      setHsv(nextHsv)
      const nextHex = hsvToHex(nextHsv)
      setHexDraft(nextHex.slice(1).toUpperCase())
      onChange(nextHex)
    },
    [onChange],
  )

  const updateSvFromClientPoint = useCallback(
    (clientX: number, clientY: number) => {
      const el = svCanvasRef.current
      if (!el) return
      const rect = el.getBoundingClientRect()
      if (rect.width <= 0 || rect.height <= 0) return
      const s = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width))
      const v = Math.min(1, Math.max(0, 1 - (clientY - rect.top) / rect.height))
      applyHsvUpdate({ h: hsv.h, s, v })
    },
    [applyHsvUpdate, hsv.h],
  )

  const updateHueFromClientX = useCallback(
    (clientX: number) => {
      const el = hueBarRef.current
      if (!el) return
      const rect = el.getBoundingClientRect()
      if (rect.width <= 0) return
      const ratio = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width))
      const h = Math.min(359.9, ratio * 360)
      applyHsvUpdate({ h, s: hsv.s, v: hsv.v })
    },
    [applyHsvUpdate, hsv.s, hsv.v],
  )

  useEffect(() => {
    if (!open) return
    const handleMouseMove = (e: MouseEvent) => {
      if (isDraggingSvRef.current) {
        updateSvFromClientPoint(e.clientX, e.clientY)
      } else if (isDraggingHueRef.current) {
        updateHueFromClientX(e.clientX)
      }
    }
    const handleMouseUp = () => {
      isDraggingSvRef.current = false
      isDraggingHueRef.current = false
    }
    window.addEventListener('mousemove', handleMouseMove)
    window.addEventListener('mouseup', handleMouseUp)
    return () => {
      window.removeEventListener('mousemove', handleMouseMove)
      window.removeEventListener('mouseup', handleMouseUp)
    }
  }, [open, updateSvFromClientPoint, updateHueFromClientX])

  const commitHexDraft = () => {
    const valid = normalizeHex(hexDraft)
    if (valid) {
      setHsv(hexToHsv(valid))
      setHexDraft(valid.slice(1).toUpperCase())
      onChange(valid)
    } else {
      setHexDraft(normalizedValue.slice(1).toUpperCase())
    }
  }

  const pureHueHex = hsvToHex({ h: hsv.h, s: 1, v: 1 })
  const currentHex = hsvToHex(hsv)

  return (
    <div className={`relative inline-flex items-center ${className}`}>
      <button
        ref={triggerRef}
        type="button"
        aria-label={ariaLabel ?? label}
        aria-expanded={open}
        aria-haspopup="dialog"
        title={label ? `${label}: ${normalizedValue.toUpperCase()}` : normalizedValue.toUpperCase()}
        onClick={() => setOpen(prev => !prev)}
        className={`group relative flex h-6 cursor-pointer items-center gap-1.5 rounded-md border px-2 transition-colors focus:outline-none ${
          open
            ? 'border-zinc-900 bg-zinc-50 dark:border-zinc-100 dark:bg-zinc-900'
            : 'border-zinc-200 bg-white hover:border-zinc-400 dark:border-zinc-800 dark:bg-zinc-950 dark:hover:border-zinc-600'
        }`}>
        {prefix && <span className="font-mono text-[10px] font-bold text-zinc-500 dark:text-zinc-400">{prefix}</span>}
        <span
          className="h-3 w-3 shrink-0 rounded-full ring-1 ring-black/15 ring-inset dark:ring-white/20"
          style={{ backgroundColor: normalizedValue }}
        />
      </button>

      {open &&
        typeof document !== 'undefined' &&
        createPortal(
          <div
            ref={popoverRef}
            role="dialog"
            aria-label={ariaLabel ?? label ?? 'Color Picker'}
            style={{
              position: 'fixed',
              top: `${geometry.top}px`,
              left: `${geometry.left}px`,
              width: `${POPOVER_WIDTH}px`,
              transformOrigin: geometry.placement === 'bottom' ? 'top right' : 'bottom right',
              transform:
                geometry.placement === 'bottom'
                  ? entered
                    ? 'translateY(0) scale(1)'
                    : 'translateY(-4px) scale(0.96)'
                  : entered
                    ? 'translateY(-100%) scale(1)'
                    : 'translateY(calc(-100% + 4px)) scale(0.96)',
              opacity: entered ? 1 : 0,
              transition:
                'opacity 140ms cubic-bezier(0.22, 1, 0.36, 1), transform 140ms cubic-bezier(0.22, 1, 0.36, 1)',
            }}
            className="z-[9999] flex flex-col gap-2.5 rounded-lg border border-zinc-200 bg-white p-2.5 shadow-2xl select-none dark:border-zinc-800 dark:bg-zinc-950">
            {/* Header label */}
            {label && (
              <div className="flex items-center justify-between">
                <span className="text-[11px] font-semibold text-zinc-700 dark:text-zinc-300">{label}</span>
                <span className="font-mono text-[10px] text-zinc-400 dark:text-zinc-500">
                  {currentHex.toUpperCase()}
                </span>
              </div>
            )}

            {/* 2D Saturation / Value Canvas */}
            <div
              ref={svCanvasRef}
              data-testid="color-picker-sv-canvas"
              onMouseDown={e => {
                e.preventDefault()
                isDraggingSvRef.current = true
                updateSvFromClientPoint(e.clientX, e.clientY)
              }}
              className="relative h-28 w-full cursor-crosshair overflow-hidden rounded-md ring-1 ring-black/10 ring-inset dark:ring-white/10"
              style={{
                backgroundColor: pureHueHex,
                backgroundImage:
                  'linear-gradient(to top, #000000, transparent), linear-gradient(to right, #ffffff, transparent)',
              }}>
              <span
                className="pointer-events-none absolute h-3.5 w-3.5 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white shadow-[0_0_0_1px_rgba(0,0,0,0.45)]"
                style={{
                  left: `${hsv.s * 100}%`,
                  top: `${(1 - hsv.v) * 100}%`,
                  backgroundColor: currentHex,
                }}
              />
            </div>

            {/* Hue Slider */}
            <div
              ref={hueBarRef}
              data-testid="color-picker-hue-slider"
              onMouseDown={e => {
                e.preventDefault()
                isDraggingHueRef.current = true
                updateHueFromClientX(e.clientX)
              }}
              className="relative h-3 w-full cursor-pointer rounded-full ring-1 ring-black/10 ring-inset dark:ring-white/10"
              style={{
                background:
                  'linear-gradient(to right, #ff0000 0%, #ffff00 17%, #00ff00 33%, #00ffff 50%, #0000ff 67%, #ff00ff 83%, #ff0000 100%)',
              }}>
              <span
                className="pointer-events-none absolute top-1/2 h-3.5 w-3.5 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white shadow-[0_0_0_1px_rgba(0,0,0,0.45)]"
                style={{
                  left: `${(hsv.h / 360) * 100}%`,
                  backgroundColor: pureHueHex,
                }}
              />
            </div>

            {/* Curated Quick Swatches */}
            <div className="grid grid-cols-6 gap-1.5 pt-0.5">
              {QUICK_SWATCHES.map(swatch => {
                const isSelected = normalizedValue === swatch.toLowerCase()
                return (
                  <button
                    key={swatch}
                    type="button"
                    aria-label={swatch.toUpperCase()}
                    onClick={() => {
                      const nextHsv = hexToHsv(swatch)
                      setHsv(nextHsv)
                      setHexDraft(swatch.slice(1).toUpperCase())
                      onChange(swatch.toLowerCase())
                    }}
                    className={`relative flex h-5 w-full cursor-pointer items-center justify-center rounded-[4px] transition-transform hover:scale-105 ${
                      isSelected ? 'ring-2 ring-zinc-900 dark:ring-white' : 'ring-1 ring-black/15 dark:ring-white/15'
                    }`}
                    style={{ backgroundColor: swatch }}>
                    {isSelected && <Check className="h-3 w-3 text-white drop-shadow-[0_1px_1px_rgba(0,0,0,0.75)]" />}
                  </button>
                )
              })}
            </div>

            {/* Hex Input Row */}
            <div className="flex items-center justify-between gap-2 pt-0.5">
              <div className="flex items-center gap-1.5">
                <span
                  className="h-5 w-5 shrink-0 rounded-[4px] ring-1 ring-black/15 ring-inset dark:ring-white/20"
                  style={{ backgroundColor: currentHex }}
                />
                <span className="text-[11px] font-medium text-zinc-500 dark:text-zinc-400">HEX</span>
              </div>
              <div className="flex h-6 items-center rounded-sm border border-zinc-200 bg-zinc-50 px-1.5 focus-within:border-zinc-500 dark:border-zinc-800 dark:bg-zinc-900 dark:focus-within:border-zinc-500">
                <span className="font-mono text-[11px] text-zinc-400 select-none">#</span>
                <input
                  type="text"
                  aria-label="HEX"
                  maxLength={6}
                  value={hexDraft}
                  onChange={e => {
                    const cleaned = e.target.value.replace(/[^0-9a-fA-F]/g, '').toUpperCase()
                    setHexDraft(cleaned)
                    if (cleaned.length === 6) {
                      const valid = `#${cleaned.toLowerCase()}`
                      setHsv(hexToHsv(valid))
                      onChange(valid)
                    }
                  }}
                  onBlur={commitHexDraft}
                  onKeyDown={e => {
                    if (e.key === 'Enter') {
                      commitHexDraft()
                      setOpen(false)
                    }
                  }}
                  className="w-14 bg-transparent pl-0.5 font-mono text-[11px] font-medium text-zinc-900 focus:outline-none dark:text-zinc-100"
                />
              </div>
            </div>
          </div>,
          document.body,
        )}
    </div>
  )
}
