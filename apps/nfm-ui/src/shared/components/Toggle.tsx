import React from 'react'

export interface ToggleProps {
  checked: boolean
  onChange: (checked: boolean) => void
  disabled?: boolean
  className?: string
  ariaLabel?: string
}

export const Toggle: React.FC<ToggleProps> = ({ checked, onChange, disabled = false, className = '', ariaLabel }) => {
  const handleKeyDown = (e: React.KeyboardEvent<HTMLButtonElement>) => {
    if (disabled) return
    if (e.key === ' ' || e.key === 'Enter') {
      e.preventDefault()
      onChange(!checked)
    }
  }

  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={ariaLabel}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      onKeyDown={handleKeyDown}
      className={`relative inline-flex h-5 w-9 shrink-0 cursor-pointer items-center rounded-full border transition-all duration-150 ease-out focus-visible:ring-1 focus-visible:ring-zinc-400 focus-visible:outline-none disabled:cursor-not-allowed disabled:opacity-45 ${
        checked
          ? 'border-zinc-900 bg-zinc-900 dark:border-zinc-100 dark:bg-zinc-100'
          : 'border-black/25 bg-black/[0.06] hover:bg-black/[0.1] dark:border-white/25 dark:bg-white/[0.06] dark:hover:bg-white/[0.1]'
      } ${className}`}>
      <span
        className={`pointer-events-none inline-block h-3.5 w-3.5 transform rounded-full transition duration-150 ease-out ${
          checked ? 'translate-x-4 bg-white dark:bg-zinc-950' : 'translate-x-0.5 bg-zinc-600 dark:bg-zinc-300'
        }`}
      />
    </button>
  )
}
