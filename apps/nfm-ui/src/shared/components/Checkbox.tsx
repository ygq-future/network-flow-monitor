import React from 'react'
import { Check } from 'lucide-react'

export interface CheckboxProps {
  checked: boolean
  onChange: (checked: boolean) => void
  disabled?: boolean
  label?: React.ReactNode
  description?: React.ReactNode
  className?: string
  ariaLabel?: string
}

export const Checkbox: React.FC<CheckboxProps> = ({
  checked,
  onChange,
  disabled = false,
  label,
  description,
  className = '',
  ariaLabel,
}) => {
  const handleKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (disabled) return
    if (e.key === ' ' || e.key === 'Enter') {
      e.preventDefault()
      onChange(!checked)
    }
  }

  return (
    <div
      role="checkbox"
      aria-checked={checked}
      aria-label={typeof label === 'string' ? label : ariaLabel}
      tabIndex={disabled ? -1 : 0}
      onClick={() => !disabled && onChange(!checked)}
      onKeyDown={handleKeyDown}
      className={`inline-flex items-start gap-2.5 select-none ${
        disabled ? 'cursor-not-allowed opacity-50' : 'cursor-pointer'
      } ${className}`}>
      <div
        className={`mt-0.5 flex h-4 w-4 shrink-0 items-center justify-center rounded border transition-colors ${
          checked
            ? 'border-zinc-900 bg-zinc-900 text-white dark:border-zinc-100 dark:bg-zinc-100 dark:text-zinc-950'
            : 'border-black/20 bg-white/80 hover:border-black/35 dark:border-white/20 dark:bg-white/[0.05] dark:hover:border-white/35'
        }`}>
        {checked && <Check className="h-3 w-3 stroke-[2.75]" />}
      </div>
      {(label || description) && (
        <div className="flex flex-col">
          {label && <span className="text-xs font-medium text-zinc-900 dark:text-zinc-100">{label}</span>}
          {description && <span className="text-[11px] text-zinc-500 dark:text-zinc-400">{description}</span>}
        </div>
      )}
    </div>
  )
}
