import React from 'react'
import { Settings, ArrowLeft } from 'lucide-react'
import { Card } from '../../shared/components/Card'
import { Button } from '../../shared/components/Button'
import { useTranslation } from '../../shared/i18n/I18nContext'

export interface SettingsPlaceholderProps {
  onBackToTracer?: () => void
}

export const SettingsPlaceholder: React.FC<SettingsPlaceholderProps> = ({ onBackToTracer }) => {
  const { t } = useTranslation()

  const sections = [
    t.settings.general,
    t.settings.appearance,
    t.settings.capsule,
    t.settings.monitor,
    t.settings.ranking,
    t.settings.collector,
    t.settings.diagnostics,
  ]

  return (
    <div className="mx-auto flex max-w-lg flex-col gap-4 p-4 select-none">
      <div className="flex items-center gap-3">
        <Button variant="ghost" size="sm" onClick={onBackToTracer} className="h-8 w-8 p-0">
          <ArrowLeft className="h-4 w-4" />
        </Button>
        <div className="flex items-center gap-2">
          <Settings className="h-5 w-5 text-blue-600 dark:text-blue-400" />
          <h1 className="text-sm font-bold text-slate-900 dark:text-slate-100">{t.settings.title}</h1>
        </div>
      </div>

      <Card className="flex flex-col gap-3">
        <p className="text-xs text-slate-600 dark:text-slate-400">{t.settings.comingSoon}</p>
        <div className="grid grid-cols-2 gap-2 pt-2">
          {sections.map(sec => (
            <div
              key={sec}
              className="flex items-center rounded-lg border border-slate-200/60 bg-slate-50/50 p-2 text-xs font-medium text-slate-700 dark:border-slate-800/60 dark:bg-slate-800/30 dark:text-slate-300">
              {sec}
            </div>
          ))}
        </div>
      </Card>
    </div>
  )
}
