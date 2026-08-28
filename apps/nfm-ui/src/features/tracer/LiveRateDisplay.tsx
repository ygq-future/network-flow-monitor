import React from 'react'
import { ArrowDown, ArrowUp } from 'lucide-react'
import { Card } from '../../shared/components/Card'
import { useTrafficTick } from '../../bridge/BridgeContext'
import { formatRate } from '../../shared/utils/formatRate'
import { useTranslation } from '../../shared/i18n/I18nContext'

export const LiveRateDisplay: React.FC = () => {
  const tick = useTrafficTick()
  const { t } = useTranslation()

  const dl = formatRate(tick.downloadBytesPerSec)
  const ul = formatRate(tick.uploadBytesPerSec)

  return (
    <div className="grid grid-cols-2 gap-4">
      {/* Download Card */}
      <Card className="relative overflow-hidden border-sky-200/60 bg-gradient-to-br from-sky-50/50 to-transparent dark:border-sky-900/40 dark:from-sky-950/20">
        <div className="mb-2 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <div className="flex h-7 w-7 items-center justify-center rounded-lg bg-sky-100 text-sky-600 dark:bg-sky-900/60 dark:text-sky-400">
              <ArrowDown className="h-4 w-4" />
            </div>
            <span className="text-xs font-semibold text-slate-600 dark:text-slate-400">{t.common.download}</span>
          </div>
          <span className="font-mono text-[10px] text-slate-400 dark:text-slate-500">#{tick.seq}</span>
        </div>
        <div className="flex items-baseline gap-1.5">
          <span className="font-mono text-2xl font-bold tracking-tight text-slate-900 tabular-nums dark:text-slate-100">
            {dl.value}
          </span>
          <span className="text-xs font-semibold text-slate-500 dark:text-slate-400">{dl.unit}</span>
        </div>
      </Card>

      {/* Upload Card */}
      <Card className="relative overflow-hidden border-violet-200/60 bg-gradient-to-br from-violet-50/50 to-transparent dark:border-violet-900/40 dark:from-violet-950/20">
        <div className="mb-2 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <div className="flex h-7 w-7 items-center justify-center rounded-lg bg-violet-100 text-violet-600 dark:bg-violet-900/60 dark:text-violet-400">
              <ArrowUp className="h-4 w-4" />
            </div>
            <span className="text-xs font-semibold text-slate-600 dark:text-slate-400">{t.common.upload}</span>
          </div>
          <span className="font-mono text-[10px] text-slate-400 dark:text-slate-500">#{tick.seq}</span>
        </div>
        <div className="flex items-baseline gap-1.5">
          <span className="font-mono text-2xl font-bold tracking-tight text-slate-900 tabular-nums dark:text-slate-100">
            {ul.value}
          </span>
          <span className="text-xs font-semibold text-slate-500 dark:text-slate-400">{ul.unit}</span>
        </div>
      </Card>
    </div>
  )
}
