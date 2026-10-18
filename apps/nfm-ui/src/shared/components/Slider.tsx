import React from 'react'

export interface SliderProps {
  value: number
  min: number
  max: number
  step?: number
  unit?: string
  onChange: (value: number) => void
  disabled?: boolean
  className?: string
  ariaLabel?: string
}

export const Slider: React.FC<SliderProps> = ({
  value,
  min,
  max,
  step = 1,
  unit = '',
  onChange,
  disabled = false,
  className = '',
  ariaLabel,
}) => {
  const percentage = max > min ? Math.max(0, Math.min(100, ((value - min) / (max - min)) * 100)) : 0

  return (
    <div className={`flex items-center gap-2.5 ${disabled ? 'opacity-45' : ''} ${className}`}>
      <div className="relative flex h-5 w-28 items-center">
        {/* Track background */}
        <div className="h-1 w-full rounded-full bg-zinc-200 dark:bg-zinc-800">
          {/* Filled track */}
          <div className="h-full rounded-full bg-zinc-900 dark:bg-zinc-100" style={{ width: `${percentage}%` }} />
        </div>
        {/* Range input */}
        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          disabled={disabled}
          aria-label={ariaLabel}
          aria-valuenow={value}
          aria-valuemin={min}
          aria-valuemax={max}
          onChange={e => onChange(Number(e.target.value))}
          className="absolute inset-0 h-full w-full cursor-pointer opacity-0"
        />
        {/* Thumb handle */}
        <div
          className="pointer-events-none absolute h-3 w-3 -translate-x-1/2 rounded-full border border-zinc-400 bg-white transition-transform hover:scale-110 dark:border-zinc-500 dark:bg-zinc-100"
          style={{ left: `${percentage}%` }}
        />
      </div>
      <span className="min-w-11 text-right font-mono text-[11px] font-medium text-zinc-700 tabular-nums dark:text-zinc-300">
        {value}
        {unit ? ` ${unit}` : ''}
      </span>
    </div>
  )
}
