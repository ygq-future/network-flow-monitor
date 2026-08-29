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
    'inline-flex items-center justify-center font-medium transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-blue-500 disabled:pointer-events-none disabled:opacity-50 cursor-pointer select-none rounded-lg'

  const sizeStyles = {
    sm: 'h-7 w-7 text-xs',
    md: 'h-8 w-8 text-sm',
    lg: 'h-9 w-9 text-base',
  }

  const variantStyles = {
    primary: 'bg-blue-600 text-white hover:bg-blue-700 active:bg-blue-800 dark:bg-blue-500 dark:hover:bg-blue-600',
    secondary:
      'bg-slate-200/80 text-slate-900 hover:bg-slate-300 active:bg-slate-300/80 dark:bg-slate-800/80 dark:text-slate-100 dark:hover:bg-slate-700',
    ghost: 'text-slate-700 hover:bg-slate-200/60 active:bg-slate-200 dark:text-slate-300 dark:hover:bg-slate-800/60',
    subtle:
      'border border-slate-200/80 bg-white/60 text-slate-700 hover:bg-slate-100 dark:border-slate-800 dark:bg-slate-900/60 dark:text-slate-300 dark:hover:bg-slate-800',
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
