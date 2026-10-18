import React from 'react'

export interface CardProps extends React.HTMLAttributes<HTMLDivElement> {
  variant?: 'default' | 'subtle' | 'outline'
}

export const Card: React.FC<CardProps> = ({ children, variant = 'default', className = '', ...props }) => {
  const baseStyles = 'relative rounded-md p-3 transition-colors duration-150 focus-within:z-20'

  const variantStyles = {
    default: 'bg-zinc-50 dark:bg-zinc-900 border border-zinc-200 dark:border-zinc-800',
    subtle: 'bg-zinc-100 dark:bg-zinc-900 border border-zinc-200 dark:border-zinc-800',
    outline: 'border border-zinc-300 dark:border-zinc-700 bg-white dark:bg-zinc-950',
  }
  return (
    <div className={`${baseStyles} ${variantStyles[variant]} ${className}`} {...props}>
      {children}
    </div>
  )
}
