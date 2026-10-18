import React from 'react'

export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'primary' | 'secondary' | 'ghost' | 'outline' | 'danger'
  size?: 'sm' | 'md' | 'lg'
}

export const Button: React.FC<ButtonProps> = ({
  children,
  variant = 'secondary',
  size = 'md',
  className = '',
  ...props
}) => {
  const baseStyles =
    'inline-flex shrink-0 items-center justify-center whitespace-nowrap font-medium transition-all duration-150 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-zinc-400 disabled:pointer-events-none disabled:opacity-45 cursor-pointer select-none rounded-md'
  const sizeStyles = {
    sm: 'h-7 px-2.5 text-xs gap-1.5',
    md: 'h-8 px-3.5 text-xs gap-2',
    lg: 'h-9 px-4 text-sm gap-2',
  }

  const variantStyles = {
    primary:
      'bg-zinc-900 text-white shadow-2xs hover:bg-zinc-800 active:bg-zinc-950 dark:bg-zinc-100 dark:text-zinc-950 dark:hover:bg-white dark:active:bg-zinc-200',
    secondary:
      'border border-black/[0.08] bg-white/80 text-zinc-800 hover:bg-white active:bg-zinc-100/80 dark:border-white/[0.1] dark:bg-white/[0.06] dark:text-zinc-100 dark:hover:bg-white/[0.1] dark:active:bg-white/[0.04]',
    ghost:
      'text-zinc-700 hover:bg-black/[0.05] active:bg-black/[0.08] dark:text-zinc-300 dark:hover:bg-white/[0.07] dark:active:bg-white/[0.04]',
    outline:
      'border border-black/[0.14] text-zinc-900 hover:bg-black/[0.04] dark:border-white/[0.14] dark:text-zinc-100 dark:hover:bg-white/[0.06]',
    danger:
      'border border-red-500/25 bg-red-500/10 text-red-700 hover:bg-red-500/15 dark:border-red-400/25 dark:bg-red-500/15 dark:text-red-200 dark:hover:bg-red-500/25',
  }
  return (
    <button className={`${baseStyles} ${sizeStyles[size]} ${variantStyles[variant]} ${className}`} {...props}>
      {children}
    </button>
  )
}
