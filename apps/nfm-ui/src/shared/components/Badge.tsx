import React from 'react'

export interface BadgeProps extends React.HTMLAttributes<HTMLSpanElement> {
  variant?: 'default' | 'success' | 'warning' | 'info' | 'danger'
}

export const Badge: React.FC<BadgeProps> = ({ children, variant = 'default', className = '', ...props }) => {
  const baseStyles = 'inline-flex items-center gap-1.5 px-2 py-0.5 rounded-full text-xs font-medium'

  const variantStyles = {
    default: 'bg-slate-200 text-slate-800 dark:bg-slate-800 dark:text-slate-200',
    success: 'bg-emerald-100 text-emerald-800 dark:bg-emerald-950/80 dark:text-emerald-300',
    warning: 'bg-amber-100 text-amber-800 dark:bg-amber-950/80 dark:text-amber-300',
    info: 'bg-sky-100 text-sky-800 dark:bg-sky-950/80 dark:text-sky-300',
    danger: 'bg-red-100 text-red-800 dark:bg-red-950/80 dark:text-red-300',
  }

  return (
    <span className={`${baseStyles} ${variantStyles[variant]} ${className}`} {...props}>
      {children}
    </span>
  )
}
