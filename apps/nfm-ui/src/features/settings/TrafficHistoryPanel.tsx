import React, { useMemo, useState } from 'react'
import {
  AppWindow,
  ArrowDown,
  ArrowUp,
  ArrowUpDown,
  Calendar,
  ChevronLeft,
  ChevronRight,
  RefreshCw,
  Search,
  X,
} from 'lucide-react'
import { TrafficHistoryAppEntryDto, TrafficHistoryDayDto, TrafficHistoryDto } from '../../bridge/bridgeTypes'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { formatBytes } from '../../shared/utils/formatRate'
import { Button } from '../../shared/components/Button'
import { Card } from '../../shared/components/Card'

export type HistoryPeriodGranularity = 'day' | 'week' | 'month' | 'quarter' | 'year'
export type HistoryScaleMode = 'logarithmic' | 'linear'
export type HistorySortField = 'name' | 'download' | 'upload' | 'total'
export type HistorySortDirection = 'asc' | 'desc'
export type HistoryRootDimension = 'time' | 'app'

export type HistoryDrillNode =
  | { kind: 'period'; periodKey: string; periodLabel: string; granularity: HistoryPeriodGranularity }
  | { kind: 'app'; appId: string; displayName: string; executableName: string; executablePath: string; iconKey: string }

export interface PeriodBucket {
  periodKey: string
  periodLabel: string
  downloadBytes: number
  uploadBytes: number
  totalBytes: number
  applications: Map<string, AggregatedAppRecord>
}

export interface AggregatedAppRecord {
  appId: string
  executablePath: string
  executableName: string
  displayName: string
  iconKey: string
  downloadBytes: number
  uploadBytes: number
  totalBytes: number
  activeDaysCount: number
}

interface ParsedLocalDate {
  year: number
  month: number
  day: number
}

function parseLocalDateYmd(dateStr: string): ParsedLocalDate | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(dateStr.trim())
  if (!m) return null
  const year = Number(m[1])
  const month = Number(m[2])
  const day = Number(m[3])
  if (!Number.isFinite(year) || !Number.isFinite(month) || !Number.isFinite(day)) return null
  return { year, month, day }
}

function pad2(n: number): string {
  return n < 10 ? `0${n}` : `${n}`
}

function shortYear2(year: number): string {
  return pad2(year % 100)
}

function formatSlashShortDate(d: Date): string {
  return `${shortYear2(d.getFullYear())}/${pad2(d.getMonth() + 1)}/${pad2(d.getDate())}`
}

export function getPeriodKeyAndLabel(
  dateStr: string,
  granularity: HistoryPeriodGranularity,
): { key: string; label: string } {
  const parsed = parseLocalDateYmd(dateStr)
  if (!parsed) {
    return { key: dateStr, label: dateStr }
  }
  const { year, month, day } = parsed
  const yy = shortYear2(year)
  if (granularity === 'day') {
    const label = `${yy}/${pad2(month)}/${pad2(day)}`
    return { key: dateStr, label }
  }
  if (granularity === 'week') {
    const dt = new Date(year, month - 1, day)
    const dayOfWeek = dt.getDay() // 0 (Sun) .. 6 (Sat)
    const offsetToMonday = dayOfWeek === 0 ? -6 : 1 - dayOfWeek
    const monday = new Date(year, month - 1, day + offsetToMonday)
    const sunday = new Date(monday.getFullYear(), monday.getMonth(), monday.getDate() + 6)
    const key = `${monday.getFullYear()}-${pad2(monday.getMonth() + 1)}-${pad2(monday.getDate())}`
    const label = `${formatSlashShortDate(monday)}~${pad2(sunday.getMonth() + 1)}/${pad2(sunday.getDate())}`
    return { key, label }
  }
  if (granularity === 'month') {
    const key = `${year}-${pad2(month)}`
    const label = `${yy}/${pad2(month)}`
    return { key, label }
  }
  if (granularity === 'quarter') {
    const q = Math.floor((month - 1) / 3) + 1
    const key = `${year}-Q${q}`
    const label = `${yy} Q${q}`
    return { key, label }
  }
  const key = `${year}`
  return { key, label: yy }
}

export function calculateHistoryBarFillPercent(
  valueBytes: number,
  maxBytes: number,
  scaleMode: HistoryScaleMode,
): { totalFillPct: number } {
  if (!Number.isFinite(valueBytes) || valueBytes <= 0 || !Number.isFinite(maxBytes) || maxBytes <= 0) {
    return { totalFillPct: 0 }
  }
  if (scaleMode === 'linear') {
    const pct = Math.min(100, Math.max(2, (valueBytes / maxBytes) * 100))
    return { totalFillPct: pct }
  }
  // Logarithmic scale anchored at 1 KB (1024 B) so KB, MB, and GB entries remain visually distinguishable
  const anchorBytes = 1024
  const logVal = Math.log10(1 + valueBytes / anchorBytes)
  const logMax = Math.log10(1 + maxBytes / anchorBytes)
  if (logMax <= 0) {
    return { totalFillPct: 0 }
  }
  const pct = Math.min(100, Math.max(4, (logVal / logMax) * 100))
  return { totalFillPct: pct }
}

function mergeAppIntoMap(map: Map<string, AggregatedAppRecord>, app: TrafficHistoryAppEntryDto): void {
  const dl = Number(app.downloadBytes) || 0
  const ul = Number(app.uploadBytes) || 0
  if (dl <= 0 && ul <= 0) return
  const key = app.appId || app.executablePath || app.executableName || app.displayName
  const existing = map.get(key)
  if (existing) {
    existing.downloadBytes += dl
    existing.uploadBytes += ul
    existing.totalBytes += dl + ul
    existing.activeDaysCount += 1
    if (!existing.executablePath && app.executablePath) existing.executablePath = app.executablePath
    if (!existing.executableName && app.executableName) existing.executableName = app.executableName
    if (!existing.displayName && app.displayName) existing.displayName = app.displayName
    if ((!existing.iconKey || !existing.iconKey.startsWith('data:')) && app.iconKey) {
      existing.iconKey = app.iconKey
    }
  } else {
    map.set(key, {
      appId: key,
      executablePath: app.executablePath || '',
      executableName: app.executableName || app.displayName || key,
      displayName: app.displayName || app.executableName || key,
      iconKey: app.iconKey || '',
      downloadBytes: dl,
      uploadBytes: ul,
      totalBytes: dl + ul,
      activeDaysCount: 1,
    })
  }
}

export function aggregatePeriods(
  days: TrafficHistoryDayDto[],
  granularity: HistoryPeriodGranularity,
  filterAppId?: string,
  withinPeriodCheck?: { granularity: HistoryPeriodGranularity; periodKey: string },
): PeriodBucket[] {
  const buckets = new Map<string, PeriodBucket>()
  for (const day of days) {
    if (withinPeriodCheck) {
      const parentPeriod = getPeriodKeyAndLabel(day.date, withinPeriodCheck.granularity)
      if (parentPeriod.key !== withinPeriodCheck.periodKey) {
        continue
      }
    }
    const { key, label } = getPeriodKeyAndLabel(day.date, granularity)
    let bucket = buckets.get(key)
    for (const app of day.applications) {
      const appKey = app.appId || app.executablePath || app.executableName || app.displayName
      if (filterAppId && appKey !== filterAppId) {
        continue
      }
      const dl = Number(app.downloadBytes) || 0
      const ul = Number(app.uploadBytes) || 0
      if (dl <= 0 && ul <= 0) continue
      if (!bucket) {
        bucket = {
          periodKey: key,
          periodLabel: label,
          downloadBytes: 0,
          uploadBytes: 0,
          totalBytes: 0,
          applications: new Map(),
        }
        buckets.set(key, bucket)
      }
      bucket.downloadBytes += dl
      bucket.uploadBytes += ul
      bucket.totalBytes += dl + ul
      mergeAppIntoMap(bucket.applications, app)
    }
  }
  return Array.from(buckets.values())
}

export function aggregateApplications(
  days: TrafficHistoryDayDto[],
  withinPeriodCheck?: { granularity: HistoryPeriodGranularity; periodKey: string },
): AggregatedAppRecord[] {
  const appMap = new Map<string, AggregatedAppRecord>()
  for (const day of days) {
    if (withinPeriodCheck) {
      const parentPeriod = getPeriodKeyAndLabel(day.date, withinPeriodCheck.granularity)
      if (parentPeriod.key !== withinPeriodCheck.periodKey) {
        continue
      }
    }
    for (const app of day.applications) {
      mergeAppIntoMap(appMap, app)
    }
  }
  return Array.from(appMap.values())
}

export interface TrafficHistoryPanelProps {
  history: TrafficHistoryDto
  downloadColorHex: string
  uploadColorHex: string
  onRefresh: () => void
  onLayoutChange?: () => void
}

interface UnifiedRowItem {
  id: string
  kind: 'period' | 'app'
  primaryLabel: string
  secondaryLabel: string
  badgeCount?: number
  badgeUnit?: string
  iconKey?: string
  executablePath?: string
  executableName?: string
  downloadBytes: number
  uploadBytes: number
  totalBytes: number
  canDrillDown: boolean
}

export const TrafficHistoryPanel: React.FC<TrafficHistoryPanelProps> = ({
  history,
  downloadColorHex,
  uploadColorHex,
  onRefresh,
  onLayoutChange,
}) => {
  const { t } = useTranslation()

  const [rootDimension, setRootDimension] = useState<HistoryRootDimension>('time')
  const [granularity, setGranularity] = useState<HistoryPeriodGranularity>('day')
  const [scaleMode, setScaleMode] = useState<HistoryScaleMode>('logarithmic')
  const [searchQuery, setSearchQuery] = useState('')
  const [sortField, setSortField] = useState<HistorySortField>('total')
  const [sortDirection, setSortDirection] = useState<HistorySortDirection>('desc')
  const [drillStack, setDrillStack] = useState<HistoryDrillNode[]>([])

  // Determine active table mode based on rootDimension + drillStack:
  // - If drillStack is empty:
  //   - rootDimension === 'time': showing period rows (sorted by date desc by default when switching to time root, or sortField)
  //   - rootDimension === 'app': showing aggregated app rows across all time
  // - If top of drillStack is 'period': showing aggregated app rows within that period
  // - If top of drillStack is 'app': showing period rows (at current granularity) for that app (optionally scoped to an ancestor period if any)
  const topDrill = drillStack.length > 0 ? drillStack[drillStack.length - 1] : null
  const showingMode: 'periods' | 'apps' = topDrill
    ? topDrill.kind === 'period'
      ? 'apps'
      : 'periods'
    : rootDimension === 'time'
      ? 'periods'
      : 'apps'

  const activePeriodConstraint = useMemo(() => {
    for (let i = drillStack.length - 1; i >= 0; i--) {
      const node = drillStack[i]
      if (node.kind === 'period') {
        return { granularity: node.granularity, periodKey: node.periodKey, periodLabel: node.periodLabel }
      }
    }
    return undefined
  }, [drillStack])

  const activeAppConstraint = useMemo(() => {
    for (let i = drillStack.length - 1; i >= 0; i--) {
      const node = drillStack[i]
      if (node.kind === 'app') {
        return node
      }
    }
    return undefined
  }, [drillStack])

  const rawRows: UnifiedRowItem[] = useMemo(() => {
    const days = history?.days ?? []
    if (showingMode === 'periods') {
      // If we are at root (no drill), and user typed a search query matching an app name,
      // or matching a period date, let's build period buckets.
      const buckets = aggregatePeriods(
        days,
        granularity,
        activeAppConstraint?.appId,
        // When drilling from Period -> App -> Period breakdown of that App, allow viewing all periods or within period
        undefined,
      )
      return buckets.map(b => ({
        id: b.periodKey,
        kind: 'period' as const,
        primaryLabel: b.periodLabel,
        secondaryLabel: b.periodKey,
        badgeCount: b.applications.size,
        badgeUnit: t.settings.historyAppsCount,
        downloadBytes: b.downloadBytes,
        uploadBytes: b.uploadBytes,
        totalBytes: b.totalBytes,
        // Can drill down into a period if we haven't already locked an app
        canDrillDown: !activeAppConstraint,
      }))
    }

    const apps = aggregateApplications(
      days,
      activePeriodConstraint
        ? { granularity: activePeriodConstraint.granularity, periodKey: activePeriodConstraint.periodKey }
        : undefined,
    )
    return apps.map(a => ({
      id: a.appId,
      kind: 'app' as const,
      primaryLabel: a.displayName,
      secondaryLabel: a.executablePath || a.executableName,
      badgeCount: a.activeDaysCount,
      badgeUnit: t.settings.historyPeriodsCount,
      iconKey: a.iconKey,
      executablePath: a.executablePath,
      executableName: a.executableName,
      downloadBytes: a.downloadBytes,
      uploadBytes: a.uploadBytes,
      totalBytes: a.totalBytes,
      // Can drill down into an app's period breakdown if not already inside an app drill
      canDrillDown: !activeAppConstraint,
    }))
  }, [history, showingMode, granularity, activeAppConstraint, activePeriodConstraint, t.settings])

  const filteredAndSortedRows = useMemo(() => {
    const q = searchQuery.trim().toLowerCase()
    const filtered = q
      ? rawRows.filter(
          row =>
            row.primaryLabel.toLowerCase().includes(q) ||
            row.secondaryLabel.toLowerCase().includes(q) ||
            (row.executableName && row.executableName.toLowerCase().includes(q)) ||
            row.id.toLowerCase().includes(q),
        )
      : rawRows

    const sorted = [...filtered].sort((a, b) => {
      let cmp = 0
      if (sortField === 'name') {
        // For periods, sort by periodKey (chronological)
        const keyA = a.kind === 'period' ? a.id : a.primaryLabel.toLowerCase()
        const keyB = b.kind === 'period' ? b.id : b.primaryLabel.toLowerCase()
        cmp = keyA.localeCompare(keyB)
      } else if (sortField === 'download') {
        cmp = a.downloadBytes - b.downloadBytes
      } else if (sortField === 'upload') {
        cmp = a.uploadBytes - b.uploadBytes
      } else {
        cmp = a.totalBytes - b.totalBytes
      }
      if (cmp === 0) {
        cmp = b.id.localeCompare(a.id)
      }
      return sortDirection === 'asc' ? cmp : -cmp
    })

    return sorted
  }, [rawRows, searchQuery, sortField, sortDirection])

  const summaryTotals = useMemo(() => {
    let dl = 0
    let ul = 0
    let maxRowTotal = 0
    for (const r of filteredAndSortedRows) {
      dl += r.downloadBytes
      ul += r.uploadBytes
      if (r.totalBytes > maxRowTotal) {
        maxRowTotal = r.totalBytes
      }
    }
    return {
      downloadBytes: dl,
      uploadBytes: ul,
      totalBytes: dl + ul,
      maxRowTotal,
    }
  }, [filteredAndSortedRows])

  const handleToggleSort = (field: HistorySortField) => {
    if (sortField === field) {
      setSortDirection(prev => (prev === 'desc' ? 'asc' : 'desc'))
    } else {
      setSortField(field)
      setSortDirection('desc')
    }
  }

  const handleSwitchRootDimension = (dim: HistoryRootDimension) => {
    setRootDimension(dim)
    setDrillStack([])
    setSortField(dim === 'time' ? 'name' : 'total')
    setSortDirection('desc')
    onLayoutChange?.()
  }

  const handleRowClick = (row: UnifiedRowItem) => {
    if (!row.canDrillDown) return
    if (row.kind === 'period') {
      setDrillStack(prev => [
        ...prev,
        {
          kind: 'period',
          periodKey: row.id,
          periodLabel: row.primaryLabel,
          granularity,
        },
      ])
      setSortField('total')
      setSortDirection('desc')
      onLayoutChange?.()
    } else if (row.kind === 'app') {
      setDrillStack(prev => [
        ...prev,
        {
          kind: 'app',
          appId: row.id,
          displayName: row.primaryLabel,
          executableName: row.executableName || row.primaryLabel,
          executablePath: row.executablePath || '',
          iconKey: row.iconKey || '',
        },
      ])
      setSortField('name')
      setSortDirection('desc')
      onLayoutChange?.()
    }
  }

  const handleBreadcrumbClick = (index: number) => {
    if (index < 0) {
      setDrillStack([])
    } else {
      setDrillStack(prev => prev.slice(0, index + 1))
    }
    onLayoutChange?.()
  }

  const renderSortIcon = (field: HistorySortField) => {
    if (sortField !== field) {
      return <ArrowUpDown className="h-2.5 w-2.5 opacity-40" />
    }
    return sortDirection === 'asc' ? (
      <ArrowUp className="h-2.5 w-2.5 text-zinc-900 dark:text-zinc-100" />
    ) : (
      <ArrowDown className="h-2.5 w-2.5 text-zinc-900 dark:text-zinc-100" />
    )
  }

  const periodOptions: { id: HistoryPeriodGranularity; label: string }[] = [
    { id: 'day', label: t.settings.historyPeriodDay },
    { id: 'week', label: t.settings.historyPeriodWeek },
    { id: 'month', label: t.settings.historyPeriodMonth },
    { id: 'quarter', label: t.settings.historyPeriodQuarter },
    { id: 'year', label: t.settings.historyPeriodYear },
  ]

  return (
    <Card className="flex flex-col gap-2 p-2.5">
      {/* Top Control Bar: Root Dimension Switch + Period Granularity + Scale Toggle + Refresh */}
      <div className="flex flex-wrap items-center justify-between gap-1.5">
        <div className="flex items-center gap-1.5">
          {/* Root Dimension Toggle: Time vs App */}
          <div
            role="group"
            aria-label="History Dimension"
            className="flex items-center rounded-md border border-zinc-200/80 bg-zinc-100/80 p-0.5 dark:border-zinc-800 dark:bg-zinc-900">
            <button
              type="button"
              onClick={() => handleSwitchRootDimension('time')}
              className={`flex h-6 cursor-pointer items-center gap-1 rounded-[4px] px-2 text-[11px] font-medium whitespace-nowrap transition-colors ${
                rootDimension === 'time'
                  ? 'bg-white font-semibold text-zinc-900 shadow-2xs dark:bg-zinc-800 dark:text-zinc-100'
                  : 'text-zinc-500 hover:text-zinc-900 dark:text-zinc-400 dark:hover:text-zinc-200'
              }`}>
              <Calendar className="h-3 w-3 shrink-0" />
              <span>{t.settings.historyByTime}</span>
            </button>
            <button
              type="button"
              onClick={() => handleSwitchRootDimension('app')}
              className={`flex h-6 cursor-pointer items-center gap-1 rounded-[4px] px-2 text-[11px] font-medium whitespace-nowrap transition-colors ${
                rootDimension === 'app'
                  ? 'bg-white font-semibold text-zinc-900 shadow-2xs dark:bg-zinc-800 dark:text-zinc-100'
                  : 'text-zinc-500 hover:text-zinc-900 dark:text-zinc-400 dark:hover:text-zinc-200'
              }`}>
              <AppWindow className="h-3 w-3 shrink-0" />
              <span>{t.settings.historyByApp}</span>
            </button>
          </div>

          {/* Granularity Selector (Day / Week / Month / Quarter / Year) */}
          <div
            role="group"
            aria-label="Period Granularity"
            className="flex items-center rounded-md border border-zinc-200/80 bg-zinc-100/80 p-0.5 dark:border-zinc-800 dark:bg-zinc-900">
            {periodOptions.map(opt => {
              const active = granularity === opt.id
              return (
                <button
                  key={opt.id}
                  type="button"
                  onClick={() => {
                    setGranularity(opt.id)
                    // If currently drilled into a period and user changes granularity, reset period drill
                    if (drillStack.some(n => n.kind === 'period')) {
                      setDrillStack(prev => prev.filter(n => n.kind !== 'period'))
                    }
                    onLayoutChange?.()
                  }}
                  className={`flex h-6 min-w-[26px] cursor-pointer items-center justify-center rounded-[4px] px-1.5 text-[11px] font-medium whitespace-nowrap transition-colors ${
                    active
                      ? 'bg-zinc-900 font-semibold text-white shadow-2xs dark:bg-zinc-100 dark:text-zinc-950'
                      : 'text-zinc-600 hover:text-zinc-900 dark:text-zinc-400 dark:hover:text-zinc-100'
                  }`}>
                  {opt.label}
                </button>
              )
            })}
          </div>
        </div>

        {/* Scale Mode (Log / Linear) + Refresh */}
        <div className="flex items-center gap-1.5">
          <div
            role="group"
            aria-label="Chart Scale"
            className="flex items-center rounded-md border border-zinc-200/80 bg-zinc-100/80 p-0.5 dark:border-zinc-800 dark:bg-zinc-900">
            <button
              type="button"
              onClick={() => setScaleMode('logarithmic')}
              className={`flex h-6 cursor-pointer items-center rounded-[4px] px-2 text-[11px] font-medium whitespace-nowrap transition-colors ${
                scaleMode === 'logarithmic'
                  ? 'bg-white font-semibold text-zinc-900 shadow-2xs dark:bg-zinc-800 dark:text-zinc-100'
                  : 'text-zinc-500 hover:text-zinc-900 dark:text-zinc-400 dark:hover:text-zinc-200'
              }`}>
              {t.settings.historyScaleLog}
            </button>
            <button
              type="button"
              onClick={() => setScaleMode('linear')}
              className={`flex h-6 cursor-pointer items-center rounded-[4px] px-2 text-[11px] font-medium whitespace-nowrap transition-colors ${
                scaleMode === 'linear'
                  ? 'bg-white font-semibold text-zinc-900 shadow-2xs dark:bg-zinc-800 dark:text-zinc-100'
                  : 'text-zinc-500 hover:text-zinc-900 dark:text-zinc-400 dark:hover:text-zinc-200'
              }`}>
              {t.settings.historyScaleLinear}
            </button>
          </div>

          <Button
            variant="outline"
            size="sm"
            aria-label={t.settings.excludedProgramsRefresh}
            title={t.settings.excludedProgramsRefresh}
            onClick={onRefresh}
            className="!h-7 !px-2">
            <RefreshCw className="h-3 w-3" />
          </Button>
        </div>
      </div>

      {/* Second Row: Breadcrumb Navigation (when drilled down) + Fuzzy Search Input */}
      <div className="flex items-center justify-between gap-2">
        {drillStack.length > 0 ? (
          <div className="flex min-w-0 flex-1 items-center gap-1 overflow-x-auto rounded-md border border-zinc-200/80 bg-zinc-50 px-2 py-1 text-[11px] dark:border-zinc-800 dark:bg-zinc-900/60">
            <button
              type="button"
              onClick={() => handleBreadcrumbClick(drillStack.length - 2)}
              title={t.settings.historyBackToRoot}
              className="mr-0.5 inline-flex h-5 w-5 shrink-0 cursor-pointer items-center justify-center rounded-xs text-zinc-600 hover:bg-zinc-200/80 hover:text-zinc-900 dark:text-zinc-400 dark:hover:bg-zinc-800 dark:hover:text-zinc-100">
              <ChevronLeft className="h-3.5 w-3.5" />
            </button>
            <button
              type="button"
              onClick={() => handleBreadcrumbClick(-1)}
              className="shrink-0 cursor-pointer font-medium text-zinc-500 hover:text-zinc-900 hover:underline dark:text-zinc-400 dark:hover:text-zinc-100">
              {rootDimension === 'time' ? t.settings.historyByTime : t.settings.historyByApp}
            </button>
            {drillStack.map((node, idx) => {
              const isLast = idx === drillStack.length - 1
              const label = node.kind === 'period' ? node.periodLabel : node.displayName
              return (
                <React.Fragment key={`${node.kind}-${idx}`}>
                  <ChevronRight className="h-3 w-3 shrink-0 text-zinc-400" />
                  <button
                    type="button"
                    disabled={isLast}
                    onClick={() => handleBreadcrumbClick(idx)}
                    className={`max-w-[150px] truncate ${
                      isLast
                        ? 'cursor-default font-semibold text-zinc-900 dark:text-zinc-100'
                        : 'cursor-pointer font-medium text-zinc-500 hover:text-zinc-900 hover:underline dark:text-zinc-400 dark:hover:text-zinc-100'
                    }`}>
                    {label}
                  </button>
                </React.Fragment>
              )
            })}
          </div>
        ) : null}

        <div className={`relative flex items-center ${drillStack.length > 0 ? 'w-48 shrink-0' : 'w-full'}`}>
          <Search className="pointer-events-none absolute left-2.5 h-3 w-3 text-zinc-400" />
          <input
            type="text"
            value={searchQuery}
            onChange={e => setSearchQuery(e.target.value)}
            placeholder={t.settings.historySearchPlaceholder}
            aria-label={t.settings.historySearchPlaceholder}
            className="h-7 w-full rounded-md border border-zinc-200 bg-zinc-50/70 pr-6 pl-7 text-[11px] text-zinc-900 placeholder:text-zinc-400 focus:border-zinc-400 focus:bg-white focus:outline-none dark:border-zinc-800 dark:bg-zinc-900/70 dark:text-zinc-100 dark:placeholder:text-zinc-500 dark:focus:border-zinc-600 dark:focus:bg-zinc-950"
          />
          {searchQuery && (
            <button
              type="button"
              aria-label={t.common.close}
              onClick={() => setSearchQuery('')}
              className="absolute right-1.5 cursor-pointer rounded-xs p-0.5 text-zinc-400 hover:text-zinc-700 dark:hover:text-zinc-200">
              <X className="h-3 w-3" />
            </button>
          )}
        </div>
      </div>

      {/* Multi-level Progressive Table */}
      <div className="flex flex-col overflow-hidden rounded-md border border-zinc-200/80 bg-white dark:border-zinc-800/80 dark:bg-zinc-950">
        {/* Sortable Table Header */}
        <div className="grid h-7 grid-cols-[minmax(0,1fr)_56px_56px_60px_84px] items-center gap-1.5 border-b border-zinc-200/80 bg-zinc-50/90 pr-[10px] pl-2.5 text-[10px] font-semibold text-zinc-500 dark:border-zinc-800/80 dark:bg-zinc-900/80 dark:text-zinc-400">
          <button
            type="button"
            onClick={() => handleToggleSort('name')}
            className="flex cursor-pointer items-center gap-1 truncate text-left hover:text-zinc-900 dark:hover:text-zinc-100">
            <span className="truncate">
              {showingMode === 'periods' ? t.settings.historyColDate : t.settings.historyColApp}
            </span>
            {renderSortIcon('name')}
          </button>

          <button
            type="button"
            onClick={() => handleToggleSort('download')}
            className="flex cursor-pointer items-center justify-end gap-0.5 text-right hover:text-zinc-900 dark:hover:text-zinc-100">
            <span>{t.settings.historyColDownload}</span>
            {renderSortIcon('download')}
          </button>

          <button
            type="button"
            onClick={() => handleToggleSort('upload')}
            className="flex cursor-pointer items-center justify-end gap-0.5 text-right hover:text-zinc-900 dark:hover:text-zinc-100">
            <span>{t.settings.historyColUpload}</span>
            {renderSortIcon('upload')}
          </button>

          <button
            type="button"
            onClick={() => handleToggleSort('total')}
            className="flex cursor-pointer items-center justify-end gap-0.5 text-right hover:text-zinc-900 dark:hover:text-zinc-100">
            <span>{t.settings.historyColTotal}</span>
            {renderSortIcon('total')}
          </button>

          <div className="text-right">{t.settings.historyColChart}</div>
        </div>

        {/* Table Body (Scrollable, max height so window stays compact within 640 DIP) */}
        <div className="max-h-[340px] [scrollbar-gutter:stable] divide-y divide-zinc-100 overflow-y-auto dark:divide-zinc-900">
          {filteredAndSortedRows.length === 0 ? (
            <div className="py-8 text-center text-xs text-zinc-400 dark:text-zinc-500">
              {(history?.days?.length ?? 0) === 0 ? t.settings.historyEmpty : t.settings.historyNoMatch}
            </div>
          ) : (
            filteredAndSortedRows.map(row => {
              const { totalFillPct } = calculateHistoryBarFillPercent(
                row.totalBytes,
                summaryTotals.maxRowTotal,
                scaleMode,
              )
              const dlRatio = row.totalBytes > 0 ? (row.downloadBytes / row.totalBytes) * 100 : 0
              const ulRatio = row.totalBytes > 0 ? Math.max(0, 100 - dlRatio) : 0
              const drillHint =
                row.kind === 'period' ? t.settings.historyDrillHintPeriod : t.settings.historyDrillHintApp

              return (
                <div
                  key={row.id}
                  role={row.canDrillDown ? 'button' : undefined}
                  tabIndex={row.canDrillDown ? 0 : undefined}
                  title={row.canDrillDown ? `${row.secondaryLabel} — ${drillHint}` : row.secondaryLabel}
                  onClick={() => handleRowClick(row)}
                  onKeyDown={e => {
                    if (row.canDrillDown && (e.key === 'Enter' || e.key === ' ')) {
                      e.preventDefault()
                      handleRowClick(row)
                    }
                  }}
                  className={`grid h-8 grid-cols-[minmax(0,1fr)_56px_56px_60px_84px] items-center gap-1.5 pr-[5px] pl-2.5 text-[11px] transition-colors ${
                    row.canDrillDown
                      ? 'cursor-pointer hover:bg-zinc-100/80 active:bg-zinc-200/50 dark:hover:bg-zinc-900/80 dark:active:bg-zinc-800/60'
                      : 'cursor-default'
                  }`}>
                  {/* First Column: Date Period OR Application Icon + Name */}
                  <div className="flex min-w-0 items-center gap-1.5">
                    {row.kind === 'period' ? (
                      <Calendar className="h-3.5 w-3.5 shrink-0 text-zinc-400 dark:text-zinc-500" />
                    ) : row.iconKey && row.iconKey.startsWith('data:image/') ? (
                      <img src={row.iconKey} alt="" className="h-4 w-4 shrink-0 object-contain" />
                    ) : (
                      <AppWindow className="h-4 w-4 shrink-0 text-zinc-400 dark:text-zinc-500" />
                    )}
                    <span className="truncate font-medium text-zinc-900 dark:text-zinc-100">{row.primaryLabel}</span>
                    {typeof row.badgeCount === 'number' && row.badgeCount > 0 && (
                      <span className="py-0.2 shrink-0 rounded-xs bg-zinc-100 px-1 font-mono text-[9px] text-zinc-500 dark:bg-zinc-800/80 dark:text-zinc-400">
                        {row.badgeCount}
                      </span>
                    )}
                    {row.canDrillDown && (
                      <ChevronRight className="ml-auto h-3 w-3 shrink-0 text-zinc-300 dark:text-zinc-600" />
                    )}
                  </div>

                  {/* Download Bytes */}
                  <div className="text-right font-mono text-[11px] text-zinc-600 tabular-nums dark:text-zinc-300">
                    {formatBytes(row.downloadBytes)}
                  </div>

                  {/* Upload Bytes */}
                  <div className="text-right font-mono text-[11px] text-zinc-500 tabular-nums dark:text-zinc-400">
                    {formatBytes(row.uploadBytes)}
                  </div>

                  {/* Total Bytes */}
                  <div className="text-right font-mono text-[11px] font-semibold text-zinc-900 tabular-nums dark:text-zinc-100">
                    {formatBytes(row.totalBytes)}
                  </div>

                  {/* Proportional Dual-Color Bar (Logarithmic or Linear) */}
                  <div className="flex items-center justify-end pl-1">
                    <div className="flex h-2 w-full overflow-hidden rounded-full bg-zinc-100 dark:bg-zinc-800/90">
                      <div
                        className="flex h-full overflow-hidden rounded-full transition-all duration-200"
                        style={{ width: `${totalFillPct.toFixed(1)}%` }}>
                        <div
                          style={{
                            width: `${dlRatio.toFixed(1)}%`,
                            backgroundColor: downloadColorHex,
                          }}
                          className="h-full"
                        />
                        <div
                          style={{
                            width: `${ulRatio.toFixed(1)}%`,
                            backgroundColor: uploadColorHex,
                          }}
                          className="h-full"
                        />
                      </div>
                    </div>
                  </div>
                </div>
              )
            })
          )}
        </div>

        {/* Footer Summary Row */}
        {filteredAndSortedRows.length > 0 && (
          <div className="grid h-7 grid-cols-[minmax(0,1fr)_56px_56px_60px_84px] items-center gap-1.5 border-t border-zinc-200/80 bg-zinc-50/90 pr-[10px] pl-2.5 text-[11px] font-semibold dark:border-zinc-800/80 dark:bg-zinc-900/80">
            <div className="truncate text-zinc-700 dark:text-zinc-300">
              {t.settings.historyTotalSummary} ({filteredAndSortedRows.length})
            </div>
            <div className="text-right font-mono text-zinc-700 tabular-nums dark:text-zinc-300">
              {formatBytes(summaryTotals.downloadBytes)}
            </div>
            <div className="text-right font-mono text-zinc-600 tabular-nums dark:text-zinc-400">
              {formatBytes(summaryTotals.uploadBytes)}
            </div>
            <div className="text-right font-mono text-zinc-900 tabular-nums dark:text-zinc-100">
              {formatBytes(summaryTotals.totalBytes)}
            </div>
            <div className="flex items-center justify-end gap-1.5 text-[10px] font-normal text-zinc-500 dark:text-zinc-400">
              <span className="inline-flex items-center gap-1">
                <span className="h-2 w-2 rounded-full" style={{ backgroundColor: downloadColorHex }} />
                <span>↓</span>
              </span>
              <span className="inline-flex items-center gap-1">
                <span className="h-2 w-2 rounded-full" style={{ backgroundColor: uploadColorHex }} />
                <span>↑</span>
              </span>
            </div>
          </div>
        )}
      </div>
    </Card>
  )
}
