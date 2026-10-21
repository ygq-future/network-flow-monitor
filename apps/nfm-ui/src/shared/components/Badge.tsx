import React from 'react'

export interface BadgeProps extends React.HTMLAttributes<HTMLSpanElement> {
  variant?: 'default' | 'success' | 'warning' | 'info' | 'danger'
}

export const Badge: React.FC<BadgeProps> = ({ children, variant = 'default', className = '', ...props }) => {
  const baseStyles =
    'inline-flex h-6 items-center gap-1 px-2 rounded-[5px] text-[11px] font-medium border transition-colors'

  const variantStyles = {
    default:
      'border-black/[0.08] bg-black/[0.04] text-zinc-700 dark:border-white/[0.1] dark:bg-white/[0.06] dark:text-zinc-300',
    success:
      'border-black/[0.1] bg-zinc-900/[0.06] text-zinc-900 dark:border-white/[0.14] dark:bg-white/[0.09] dark:text-zinc-100',
    warning:
      'border-amber-500/25 bg-amber-500/10 text-amber-800 dark:border-amber-400/25 dark:bg-amber-400/10 dark:text-amber-200',
    info: 'border-black/[0.08] bg-black/[0.04] text-zinc-800 dark:border-white/[0.1] dark:bg-white/[0.06] dark:text-zinc-200',
    danger: 'border-red-500/25 bg-red-500/10 text-red-700 dark:border-red-400/25 dark:bg-red-400/10 dark:text-red-200',
  }
  return (
    <span className={`${baseStyles} ${variantStyles[variant]} ${className}`} {...props}>
      {children}
    </span>
  )
}
