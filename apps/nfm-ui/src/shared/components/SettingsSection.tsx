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
    <Card className={`flex flex-col gap-3 p-4 ${className}`}>
      <div className="flex items-center justify-between border-b border-slate-200/60 pb-3 dark:border-slate-800/60">
        <div className="flex items-center gap-2.5">
          {Icon && (
            <div className="flex h-7 w-7 items-center justify-center rounded-lg bg-blue-50 text-blue-600 dark:bg-blue-950/50 dark:text-blue-400">
              <Icon className="h-4 w-4" />
            </div>
          )}
          <div className="flex flex-col">
            <h2 className="text-xs font-bold tracking-tight text-slate-900 dark:text-slate-100">{title}</h2>
            {description && <p className="text-[11px] text-slate-500 dark:text-slate-400">{description}</p>}
          </div>
        </div>
        {actions && <div className="flex items-center gap-2">{actions}</div>}
      </div>
      <div className="flex flex-col divide-y divide-slate-100 dark:divide-slate-800/40">{children}</div>
    </Card>
  )
}
