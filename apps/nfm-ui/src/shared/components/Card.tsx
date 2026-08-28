import React from 'react'

export interface CardProps extends React.HTMLAttributes<HTMLDivElement> {
  variant?: 'default' | 'subtle' | 'outline'
}

export const Card: React.FC<CardProps> = ({ children, variant = 'default', className = '', ...props }) => {
  const baseStyles = 'rounded-xl p-4 transition-all duration-200'

  const variantStyles = {
    default:
      'bg-white/80 dark:bg-slate-900/80 backdrop-blur-md border border-slate-200/80 dark:border-slate-800/80 shadow-xs',
    subtle: 'bg-slate-100/60 dark:bg-slate-800/40 border border-transparent',
    outline: 'border border-slate-300 dark:border-slate-700 bg-transparent',
  }

  return (
    <div className={`${baseStyles} ${variantStyles[variant]} ${className}`} {...props}>
      {children}
    </div>
  )
}
