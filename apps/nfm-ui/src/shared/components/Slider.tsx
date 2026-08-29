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
    <div className={`flex items-center gap-3 ${disabled ? 'opacity-50' : ''} ${className}`}>
      <div className="relative flex h-5 w-36 items-center">
        {/* Track background */}
        <div className="h-1.5 w-full rounded-full bg-slate-200 dark:bg-slate-700">
          {/* Filled track */}
          <div className="h-full rounded-full bg-blue-600 dark:bg-blue-500" style={{ width: `${percentage}%` }} />
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
          className="pointer-events-none absolute h-4 w-4 -translate-x-1/2 rounded-full border-2 border-white bg-blue-600 shadow-sm transition-transform hover:scale-110 dark:border-slate-900 dark:bg-blue-400"
          style={{ left: `${percentage}%` }}
        />
      </div>
      <span className="min-w-12 text-right font-mono text-xs font-semibold text-slate-700 dark:text-slate-300">
        {value}
        {unit ? ` ${unit}` : ''}
      </span>
    </div>
  )
}
