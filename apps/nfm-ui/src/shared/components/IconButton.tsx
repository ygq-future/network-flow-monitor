import React from 'react'

export interface IconButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'primary' | 'secondary' | 'ghost' | 'subtle'
  size?: 'sm' | 'md' | 'lg'
  'aria-label': string
}

export const IconButton: React.FC<IconButtonProps> = ({
  children,
  variant = 'ghost',
  size = 'md',
  className = '',
  'aria-label': ariaLabel,
  ...props
}) => {
  const baseStyles =
    'inline-flex items-center justify-center font-medium transition-all duration-150 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-zinc-400 disabled:pointer-events-none disabled:opacity-45 cursor-pointer select-none rounded-md'

  const sizeStyles = {
    sm: 'h-6 w-6 text-xs',
    md: 'h-7 w-7 text-xs',
    lg: 'h-8 w-8 text-sm',
  }

  const variantStyles = {
    primary:
      'bg-zinc-900 text-white hover:bg-zinc-800 active:bg-zinc-950 dark:bg-zinc-100 dark:text-zinc-950 dark:hover:bg-white',
    secondary:
      'border border-black/[0.08] bg-white/75 text-zinc-800 hover:bg-white dark:border-white/[0.1] dark:bg-white/[0.06] dark:text-zinc-100 dark:hover:bg-white/[0.12]',
    ghost:
      'text-zinc-600 hover:bg-black/[0.06] hover:text-zinc-900 active:bg-black/[0.1] dark:text-zinc-400 dark:hover:bg-white/[0.08] dark:hover:text-zinc-100',
    subtle:
      'border border-black/[0.07] bg-white/60 text-zinc-700 hover:bg-white dark:border-white/[0.08] dark:bg-white/[0.04] dark:text-zinc-300 dark:hover:bg-white/[0.09]',
  }
  return (
    <button
      aria-label={ariaLabel}
      className={`${baseStyles} ${sizeStyles[size]} ${variantStyles[variant]} ${className}`}
      {...props}>
      {children}
    </button>
  )
}
