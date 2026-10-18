import React from 'react'
import { Card } from './Card'

export interface SettingsSectionProps {
  title: string
  description?: string
  icon?: React.ComponentType<{ className?: string }>
  children: React.ReactNode
  className?: string
  actions?: React.ReactNode
}

export const SettingsSection: React.FC<SettingsSectionProps> = ({
  title,
  description,
  icon: Icon,
  children,
  className = '',
  actions,
}) => {
  return (
    <Card className={`flex flex-col gap-1.5 p-2.5 ${className}`}>
      <div className="flex items-center justify-between gap-3 border-b border-zinc-200 pb-2 dark:border-zinc-800">
        <div className="flex min-w-0 items-center gap-2">
          {Icon && (
            <div className="flex h-6 w-6 shrink-0 items-center justify-center rounded-sm border border-zinc-200 bg-white text-zinc-900 dark:border-zinc-800 dark:bg-zinc-950 dark:text-zinc-100">
              <Icon className="h-3.5 w-3.5" />
            </div>
          )}
          <div className="flex min-w-0 items-baseline gap-2">
            <h2 className="text-xs font-semibold tracking-tight text-zinc-900 dark:text-zinc-100">{title}</h2>
            {description && <p className="truncate text-[11px] text-zinc-500 dark:text-zinc-400">{description}</p>}
          </div>
        </div>
        {actions && <div className="flex shrink-0 items-center gap-1.5">{actions}</div>}
      </div>
      <div className="flex flex-col divide-y divide-zinc-200/70 dark:divide-zinc-800/80">{children}</div>
    </Card>
  )
}
