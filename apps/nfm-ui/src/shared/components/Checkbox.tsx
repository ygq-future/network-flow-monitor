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
            ? 'border-blue-600 bg-blue-600 text-white dark:border-blue-500 dark:bg-blue-500'
            : 'border-slate-300 bg-white hover:border-slate-400 dark:border-slate-700 dark:bg-slate-900 dark:hover:border-slate-600'
        }`}>
        {checked && <Check className="h-3 w-3 stroke-[3]" />}
      </div>
      {(label || description) && (
        <div className="flex flex-col">
          {label && <span className="text-xs font-medium text-slate-900 dark:text-slate-100">{label}</span>}
          {description && <span className="text-[11px] text-slate-500 dark:text-slate-400">{description}</span>}
        </div>
      )}
    </div>
  )
}
