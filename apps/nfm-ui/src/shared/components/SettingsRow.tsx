import React from 'react'

export interface SettingsRowProps {
  label: string
  description?: string
  control: React.ReactNode
  badge?: React.ReactNode
  className?: string
}

export const SettingsRow: React.FC<SettingsRowProps> = ({ label, description, control, badge, className = '' }) => {
  return (
    <div className={`flex items-center justify-between gap-3 py-1.5 first:pt-0.5 last:pb-0 ${className}`}>
      <div className="flex min-w-0 flex-col pr-2">
        <div className="flex items-center gap-1.5">
          <span className="text-xs font-medium text-zinc-900 dark:text-zinc-100">{label}</span>
          {badge}
        </div>
        {description && (
          <span className="truncate text-[11px] leading-snug text-zinc-500 dark:text-zinc-400">{description}</span>
        )}
      </div>
      <div className="flex shrink-0 items-center">{control}</div>
    </div>
  )
}
