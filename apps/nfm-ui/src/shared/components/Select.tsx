import { useState, useRef, useEffect, useLayoutEffect, useCallback } from 'react'
import { createPortal } from 'react-dom'
import { ChevronDown, Check } from 'lucide-react'

export interface SelectOption<T extends string = string> {
  value: T
  label: string
}

export interface SelectProps<T extends string = string> {
  value: T
  onChange: (value: T) => void
  options: SelectOption<T>[]
  className?: string
}

interface DropdownGeometry {
  top: number
  left: number
  width: number
  placement: 'bottom' | 'top'
}

export function Select<T extends string = string>({ value, onChange, options, className = '' }: SelectProps<T>) {
  const [open, setOpen] = useState(false)
  const [entered, setEntered] = useState(false)
  const [geometry, setGeometry] = useState<DropdownGeometry>({
    top: 0,
    left: 0,
    width: 164,
    placement: 'bottom',
  })
  const triggerRef = useRef<HTMLDivElement>(null)
  const menuRef = useRef<HTMLDivElement>(null)
  const selected = options.find(opt => opt.value === value) ?? options[0]

  const updatePosition = useCallback(() => {
    const triggerEl = triggerRef.current
    if (!triggerEl) return
    const rect = triggerEl.getBoundingClientRect()
    const viewportHeight = window.innerHeight || 390
    const estimatedMenuHeight = options.length * 26 + 10
    const spaceBelow = viewportHeight - rect.bottom - 8
    const spaceAbove = rect.top - 8
    const placement: 'bottom' | 'top' = spaceBelow < estimatedMenuHeight && spaceAbove > spaceBelow ? 'top' : 'bottom'

    setGeometry({
      top: placement === 'bottom' ? rect.bottom + 4 : rect.top - 4,
      left: rect.left,
      width: Math.max(rect.width, 164),
      placement,
    })
  }, [options.length])

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
      if (triggerRef.current?.contains(target) || menuRef.current?.contains(target)) {
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

  return (
    <div className={`relative inline-flex items-center ${className}`}>
      <div
        ref={triggerRef}
        role="button"
        tabIndex={0}
        aria-expanded={open}
        aria-haspopup="listbox"
        onClick={() => setOpen(prev => !prev)}
        onKeyDown={e => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault()
            setOpen(prev => !prev)
          }
        }}
        className="inline-flex h-6 w-[164px] cursor-pointer items-center justify-between gap-1.5 rounded-sm border border-zinc-300 bg-white px-2 text-xs font-medium text-zinc-900 transition-colors hover:border-zinc-400 hover:bg-zinc-50 focus:outline-none dark:border-zinc-700 dark:bg-zinc-950 dark:text-zinc-100 dark:hover:border-zinc-600 dark:hover:bg-zinc-900">
        <span className="truncate text-zinc-900 dark:text-zinc-100">{selected?.label ?? value}</span>
        <ChevronDown
          className={`h-3.5 w-3.5 shrink-0 text-zinc-600 transition-transform duration-150 dark:text-zinc-300 ${
            open ? 'rotate-180' : ''
          }`}
        />
      </div>

      {open &&
        typeof document !== 'undefined' &&
        createPortal(
          <div
            ref={menuRef}
            role="listbox"
            style={{
              position: 'fixed',
              top: `${geometry.top}px`,
              left: `${geometry.left}px`,
              width: `${geometry.width}px`,
              transformOrigin: geometry.placement === 'bottom' ? 'top center' : 'bottom center',
              transform:
                geometry.placement === 'bottom'
                  ? entered
                    ? 'translateY(0) scaleY(1)'
                    : 'translateY(-4px) scaleY(0.95)'
                  : entered
                    ? 'translateY(-100%) scaleY(1)'
                    : 'translateY(calc(-100% + 4px)) scaleY(0.95)',
              opacity: entered ? 1 : 0,
              transition:
                'opacity 140ms cubic-bezier(0.22, 1, 0.36, 1), transform 140ms cubic-bezier(0.22, 1, 0.36, 1)',
            }}
            className="z-[9999] overflow-hidden rounded-sm border border-zinc-300 bg-white p-1 shadow-xl select-none dark:border-zinc-700 dark:bg-zinc-950">
            {options.map(opt => {
              const isSelected = opt.value === value
              return (
                <button
                  key={opt.value}
                  type="button"
                  role="option"
                  aria-selected={isSelected}
                  onClick={() => {
                    onChange(opt.value)
                    setOpen(false)
                  }}
                  className={`flex w-full cursor-pointer items-center justify-between gap-1.5 rounded-sm px-2 py-1 text-left text-xs transition-colors ${
                    isSelected
                      ? 'bg-zinc-100 font-semibold text-zinc-950 dark:bg-zinc-800 dark:text-white'
                      : 'text-zinc-700 hover:bg-zinc-50 dark:text-zinc-300 dark:hover:bg-zinc-900'
                  }`}>
                  <span className="truncate">{opt.label}</span>
                  {isSelected && <Check className="h-3.5 w-3.5 shrink-0 text-zinc-900 dark:text-zinc-100" />}
                </button>
              )
            })}
          </div>,
          document.body,
        )}
    </div>
  )
}
