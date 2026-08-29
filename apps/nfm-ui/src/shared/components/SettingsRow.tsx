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
    <div className={`flex items-center justify-between py-2.5 first:pt-1 last:pb-1 ${className}`}>
      <div className="flex flex-col pr-4">
        <div className="flex items-center gap-2">
          <span className="text-xs font-medium text-slate-800 dark:text-slate-200">{label}</span>
          {badge}
        </div>
        {description && <span className="text-[11px] text-slate-500 dark:text-slate-400">{description}</span>}
      </div>
      <div className="flex shrink-0 items-center">{control}</div>
    </div>
  )
}
