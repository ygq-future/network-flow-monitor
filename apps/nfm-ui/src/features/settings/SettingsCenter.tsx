import React, { useState, useEffect, useLayoutEffect, useRef } from 'react'
import {
  Sliders,
  Layers,
  Activity,
  FolderOpen,
  X,
  RefreshCw,
  RotateCw,
  AlertCircle,
  Download,
  Plus,
  Search,
  Check,
  ChevronDown,
  ChevronUp,
  AppWindow,
  BarChart3,
} from 'lucide-react'
import { useBridge } from '../../bridge/BridgeContext'
import {
  AppConfigDto,
  ConfigSchemaDto,
  ConfigChangedDto,
  CollectorStatusDto,
  UpdateStatusDto,
  BridgeErrorDto,
  ConnectedAppDto,
  ConnectedApplicationsDto,
  TrafficHistoryDto,
  SurfaceChangedDto,
} from '../../bridge/bridgeTypes'
import { defaultMockConfigChanged } from '../../bridge/nativeBridge'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { useTheme } from '../../shared/theme/ThemeContext'
import { Button } from '../../shared/components/Button'
import { IconButton } from '../../shared/components/IconButton'
import { Select } from '../../shared/components/Select'
import { ColorPicker } from '../../shared/components/ColorPicker'
import { Slider } from '../../shared/components/Slider'
import { Toggle } from '../../shared/components/Toggle'
import { Badge } from '../../shared/components/Badge'
import { Card } from '../../shared/components/Card'
import { SettingsRow } from '../../shared/components/SettingsRow'
import { AppLogo } from '../../shared/components/AppLogo'
import { TrafficHistoryPanel } from './TrafficHistoryPanel'

export type SettingsTab = 'general' | 'capsule' | 'history' | 'collector'
export type LegacySettingsTab = SettingsTab | 'appearance' | 'monitor' | 'ranking' | 'diagnostics'

export interface SettingsCenterProps {
  initialTab?: LegacySettingsTab
  onBack?: () => void
  onStartDrag?: () => void
}

function normalizeSettingsTab(tab: LegacySettingsTab): SettingsTab {
  if (tab === 'capsule' || tab === 'monitor') return 'capsule'
  if (tab === 'history') return 'history'
  if (tab === 'collector' || tab === 'diagnostics') return 'collector'
  return 'general'
}

interface CapsuleColorPreset {
  id: string
  downloadHex: string
  uploadHex: string
}

const CAPSULE_COLOR_PRESETS: CapsuleColorPreset[] = [
  { id: 'fresh-breeze', downloadHex: '#38bdf8', uploadHex: '#2dd4bf' },
  { id: 'mint-lime', downloadHex: '#10b981', uploadHex: '#84cc16' },
  { id: 'azure-violet', downloadHex: '#60a5fa', uploadHex: '#a78bfa' },
  { id: 'coral-amber', downloadHex: '#fb7185', uploadHex: '#fbbf24' },
  { id: 'mono-frost', downloadHex: '#e4e4e7', uploadHex: '#71717a' },
]

function normalizeProgToken(val: string): string {
  return val
    .trim()
    .replace(/^"+|"+$/g, '')
    .replace(/\//g, '\\')
    .toLowerCase()
}

function extractFileNameToken(val: string): string {
  const clean = normalizeProgToken(val).replace(/^executable:/, '')
  const idx = clean.lastIndexOf('\\')
  return idx >= 0 ? clean.slice(idx + 1) : clean
}

function stripExeSuffix(val: string): string {
  return val.endsWith('.exe') ? val.slice(0, -4) : val
}

function isAppMatchingExcluded(app: ConnectedAppDto, excludedList: string[]): boolean {
  if (excludedList.length === 0) return false
  const appExe = extractFileNameToken(app.executableName || app.executablePath || app.displayName)
  const appStem = stripExeSuffix(appExe)
  const appPath = normalizeProgToken(app.executablePath)
  const appDisplay = normalizeProgToken(app.displayName)
  return excludedList.some(rawRule => {
    const rule = normalizeProgToken(rawRule)
    if (!rule) return false
    const ruleExe = extractFileNameToken(rule)
    const ruleStem = stripExeSuffix(ruleExe)
    return (
      rule === appExe ||
      ruleExe === appExe ||
      (ruleStem.length > 0 && (ruleStem === appStem || ruleStem === stripExeSuffix(appDisplay))) ||
      (appPath.length > 0 && rule === appPath)
    )
  })
}
export const SettingsCenter: React.FC<SettingsCenterProps> = ({ initialTab = 'general', onBack, onStartDrag }) => {
  const { t, setLocale } = useTranslation()
  const { setTheme } = useTheme()
  const bridge = useBridge()

  const [activeTab, setActiveTab] = useState<SettingsTab>(() => normalizeSettingsTab(initialTab))
  const [config, setConfig] = useState<AppConfigDto>(defaultMockConfigChanged.config)
  const [schema, setSchema] = useState<ConfigSchemaDto>(defaultMockConfigChanged.schema)
  const [collectorStatus, setCollectorStatus] = useState<CollectorStatusDto>({
    monotonicMs: Date.now(),
    etwSessionActive: true,
    droppedPackets: 0,
    activePids: 18,
    state: 'Running',
    processId: 2844,
    instanceId: 'session_active',
    isElevated: true,
  })
  const [updateStatus, setUpdateStatus] = useState<UpdateStatusDto>({
    state: 'Idle',
    currentVersion: '0.3.0',
    latestVersion: '',
    downloadProgressPercent: 0,
    errorMessage: '',
  })
  const [errorMessage, setErrorMessage] = useState<string | null>(null)
  const [connectedApps, setConnectedApps] = useState<ConnectedAppDto[]>([])
  const [trafficHistory, setTrafficHistory] = useState<TrafficHistoryDto>({
    schemaVersion: 1,
    days: [],
  })
  const [historyLayoutSeq, setHistoryLayoutSeq] = useState(0)
  const [showExclusionPanel, setShowExclusionPanel] = useState(false)
  const [excludeFilterInput, setExcludeFilterInput] = useState('')
  const [selectedConnectedAppId, setSelectedConnectedAppId] = useState<string | null>(null)
  const contentRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    setActiveTab(normalizeSettingsTab(initialTab))
    setShowExclusionPanel(false)
    setSelectedConnectedAppId(null)
    setExcludeFilterInput('')
  }, [initialTab])
  useEffect(() => {
    const unsubConfig = bridge.on<ConfigChangedDto>('configChanged', data => {
      if (data && data.config) {
        setConfig(data.config)
      }
      if (data && data.schema) {
        setSchema(data.schema)
      }
      setErrorMessage(null)
    })

    const unsubCollector = bridge.on<CollectorStatusDto>('collectorStatus', data => {
      if (data) {
        setCollectorStatus(prev => ({ ...prev, ...data }))
      }
    })
    const unsubUpdate = bridge.on<UpdateStatusDto>('updateStatus', data => {
      if (data) {
        setUpdateStatus(prev => ({ ...prev, ...data }))
      }
    })

    const unsubConnectedApps = bridge.on<ConnectedApplicationsDto>('connectedApplications', data => {
      if (data && Array.isArray(data.applications)) {
        setConnectedApps(data.applications)
      }
    })
    const unsubTrafficHistory = bridge.on<TrafficHistoryDto>('trafficHistoryUpdated', data => {
      if (data && Array.isArray(data.days)) {
        setTrafficHistory(data)
      }
    })
    const unsubSurface = bridge.on<SurfaceChangedDto>('surfaceChanged', data => {
      if (data && data.surface === 'settings') {
        setShowExclusionPanel(false)
        setSelectedConnectedAppId(null)
        setExcludeFilterInput('')
        if (data.route) {
          setActiveTab(normalizeSettingsTab(data.route as LegacySettingsTab))
        }
      }
    })
    const unsubError = bridge.on<BridgeErrorDto>('error', err => {
      if (err) {
        setErrorMessage(`${err.code}: ${err.message}`)
      }
    })

    bridge.sendCommand('requestConfig')

    return () => {
      unsubConfig()
      unsubCollector()
      unsubUpdate()
      unsubError()
      unsubConnectedApps()
      unsubTrafficHistory()
      unsubSurface()
    }
  }, [bridge])

  // Dynamically resize Settings window to fit current tab content without vertical scrollbar
  useLayoutEffect(() => {
    const el = contentRef.current
    if (!el) return
    const reportHeight = () => {
      const contentHeight = el.scrollHeight
      // 36px fused header + contentHeight + 2px border
      const desiredHeightDip = Math.min(640, Math.max(150, 38 + contentHeight))
      bridge.sendCommand('resizeSettingsWindow', { heightDip: desiredHeightDip })
    }
    reportHeight()
    const rafId = requestAnimationFrame(reportHeight)
    return () => cancelAnimationFrame(rafId)
  }, [
    activeTab,
    bridge,
    config,
    collectorStatus.lastError,
    updateStatus.state,
    updateStatus.errorMessage,
    errorMessage,
    showExclusionPanel,
    connectedApps.length,
    excludeFilterInput,
    trafficHistory.days.length,
    historyLayoutSeq,
  ])
  const handleUpdate = (key: string, value: unknown) => {
    bridge.sendCommand('updateSetting', { key, value })
  }
  const excludedPrograms = config.ranking.excludedPrograms ?? []

  const handleAddExcludedProgram = (rawEntry: string) => {
    const trimmed = rawEntry.trim()
    if (!trimmed) return
    const nextNorm = stripExeSuffix(extractFileNameToken(trimmed))
    const exists = excludedPrograms.some(item => stripExeSuffix(extractFileNameToken(item)) === nextNorm)
    if (!exists) {
      handleUpdate('ranking.excluded_programs', [...excludedPrograms, trimmed])
    }
    setExcludeFilterInput('')
  }

  const handleRemoveExcludedProgram = (target: string) => {
    handleUpdate(
      'ranking.excluded_programs',
      excludedPrograms.filter(item => item !== target),
    )
  }

  const filteredConnectedApps = connectedApps.filter(app => {
    const q = excludeFilterInput.trim().toLowerCase()
    if (!q) return true
    return (
      app.displayName.toLowerCase().includes(q) ||
      app.executableName.toLowerCase().includes(q) ||
      app.executablePath.toLowerCase().includes(q)
    )
  })

  const selectedConnectedApp =
    selectedConnectedAppId != null
      ? (filteredConnectedApps.find(
          app => app.id === selectedConnectedAppId && !isAppMatchingExcluded(app, excludedPrograms),
        ) ?? null)
      : null
  const selectedConnectedAppTarget = selectedConnectedApp
    ? selectedConnectedApp.executableName || selectedConnectedApp.displayName || selectedConnectedApp.id
    : ''
  const pendingAddTarget = selectedConnectedAppTarget || excludeFilterInput.trim()
  const downloadColor = (config.capsule.downloadColorHex || '#38bdf8').toLowerCase()
  const uploadColor = (config.capsule.uploadColorHex || '#2dd4bf').toLowerCase()

  const tabs: { id: SettingsTab; label: string; icon: React.ComponentType<{ className?: string }> }[] = [
    { id: 'general', label: t.settings.general, icon: Sliders },
    { id: 'capsule', label: t.settings.capsule, icon: Layers },
    { id: 'history', label: t.settings.history, icon: BarChart3 },
    { id: 'collector', label: t.settings.collector, icon: Activity },
  ]
  return (
    <div className="flex h-full w-full flex-col select-none">
      {/* Unified Title Bar: App Icon + Title + Fused Segmented Tabs + Close Button */}
      <header
        onMouseDown={event => {
          if (event.button === 0 && !(event.target as HTMLElement).closest('button')) {
            onStartDrag?.()
          }
        }}
        className="flex h-9 shrink-0 cursor-move items-center justify-between gap-2 border-b border-zinc-200 bg-zinc-50 px-3 dark:border-zinc-800 dark:bg-zinc-900">
        <div className="flex shrink-0 items-center gap-2">
          <AppLogo className="h-4 w-4 shrink-0" />
          <h1 className="text-xs font-semibold tracking-tight whitespace-nowrap text-zinc-900 dark:text-zinc-100">
            {t.settings.title}
          </h1>
        </div>

        <nav className="flex items-center gap-0.5 rounded-md border border-zinc-200/80 bg-zinc-200/50 p-0.5 dark:border-zinc-800 dark:bg-zinc-950/60">
          {tabs.map(tab => {
            const Icon = tab.icon
            const isActive = activeTab === tab.id
            return (
              <button
                key={tab.id}
                type="button"
                onClick={() => {
                  setActiveTab(tab.id)
                  setShowExclusionPanel(false)
                  setSelectedConnectedAppId(null)
                  setExcludeFilterInput('')
                }}
                className={`relative flex h-6 cursor-pointer items-center justify-center gap-1 rounded-[5px] px-2 text-[11px] font-medium whitespace-nowrap transition-colors ${
                  isActive
                    ? 'bg-zinc-900 font-semibold text-white shadow-xs dark:bg-zinc-100 dark:text-zinc-950'
                    : 'text-zinc-600 hover:bg-zinc-200/80 hover:text-zinc-900 dark:text-zinc-400 dark:hover:bg-zinc-800 dark:hover:text-zinc-100'
                }`}>
                <Icon
                  className={`h-3 w-3 shrink-0 ${
                    isActive ? 'text-white dark:text-zinc-950' : 'text-zinc-500 dark:text-zinc-400'
                  }`}
                />
                <span>{tab.label}</span>
              </button>
            )
          })}
        </nav>

        <div className="flex shrink-0 items-center gap-2">
          {errorMessage && (
            <div className="flex items-center gap-1.5 rounded-sm border border-red-500/30 bg-red-500/10 px-2 py-0.5 text-[11px] font-medium text-red-700 dark:border-red-400/30 dark:bg-red-400/10 dark:text-red-300">
              <AlertCircle className="h-3 w-3 shrink-0" />
              <span>{errorMessage}</span>
            </div>
          )}
          {onBack && (
            <IconButton aria-label={t.common.close} variant="ghost" size="sm" onClick={onBack}>
              <X className="h-3.5 w-3.5" />
            </IconButton>
          )}
        </div>
      </header>

      {/* Bottom Dynamic Inspector Content */}
      <div className="flex min-h-0 flex-1 flex-col overflow-hidden bg-white dark:bg-zinc-950">
        <div ref={contentRef} className="flex min-w-0 flex-col gap-2.5 p-3">
          {/* 1. General Tab */}
          {activeTab === 'general' && (
            <Card className="flex flex-col divide-y divide-zinc-200/70 px-3 py-2 dark:divide-zinc-800/80">
              <SettingsRow
                label={t.settings.launchAtStartup}
                description={t.settings.launchAtStartupDesc}
                control={
                  <Toggle
                    checked={config.general.uiAtLogin}
                    onChange={val => handleUpdate('startup.ui_at_login', val)}
                    ariaLabel={t.settings.launchAtStartup}
                  />
                }
              />
              <SettingsRow
                label={t.settings.speedUnit}
                description={t.settings.speedUnitDesc}
                control={
                  <Select
                    value={config.general.speedUnit}
                    onChange={val => handleUpdate('display.speed_unit', val)}
                    options={[
                      { value: 'auto', label: t.settings.speedUnitAuto },
                      { value: 'bytes', label: t.settings.speedUnitBytes },
                      { value: 'bits', label: t.settings.speedUnitBits },
                    ]}
                  />
                }
              />
              <SettingsRow
                label={t.settings.theme}
                description={t.settings.themeDesc}
                control={
                  <Select
                    value={config.appearance.theme}
                    onChange={val => {
                      setTheme(val === 'light' ? 'Light' : val === 'dark' ? 'Dark' : 'System')
                      handleUpdate('appearance.theme', val)
                    }}
                    options={[
                      { value: 'system', label: t.settings.themeSystem },
                      { value: 'light', label: t.settings.themeLight },
                      { value: 'dark', label: t.settings.themeDark },
                    ]}
                  />
                }
              />
              <SettingsRow
                label={t.settings.language}
                description={t.settings.languageDesc}
                control={
                  <Select
                    value={config.appearance.language}
                    onChange={val => {
                      setLocale(
                        val === 'english' ? 'English' : val === 'simplified_chinese' ? 'SimplifiedChinese' : 'System',
                      )
                      handleUpdate('appearance.language', val)
                    }}
                    options={[
                      { value: 'system', label: t.settings.langSystem },
                      { value: 'english', label: t.settings.langEnglish },
                      { value: 'simplified_chinese', label: t.settings.langChinese },
                    ]}
                  />
                }
              />
              <SettingsRow
                label={t.settings.topN}
                description={t.settings.topNDesc}
                control={
                  <Slider
                    value={config.ranking.topN}
                    min={schema.topN.min}
                    max={schema.topN.max}
                    onChange={val => handleUpdate('ranking.top_n', val)}
                    ariaLabel={t.settings.topN}
                  />
                }
              />
              <SettingsRow
                label={t.settings.rankingWindow}
                description={t.settings.rankingWindowDesc}
                control={
                  <Slider
                    value={config.ranking.windowMs}
                    min={schema.rankingWindow.min}
                    max={schema.rankingWindow.max}
                    step={schema.rankingWindow.step}
                    unit={schema.rankingWindow.unit}
                    onChange={val => handleUpdate('ranking.window_ms', val)}
                    ariaLabel={t.settings.rankingWindow}
                  />
                }
              />
              <SettingsRow
                label={t.settings.refreshInterval}
                description={t.settings.refreshIntervalDesc}
                control={
                  <Slider
                    value={config.ranking.refreshIntervalMs}
                    min={schema.refreshInterval.min}
                    max={schema.refreshInterval.max}
                    step={schema.refreshInterval.step}
                    unit={schema.refreshInterval.unit}
                    onChange={val => handleUpdate('display.refresh_interval_ms', val)}
                    ariaLabel={t.settings.refreshInterval}
                  />
                }
              />
              <div className="flex flex-col py-1.5">
                <div className="flex items-center justify-between gap-3">
                  <div className="flex min-w-0 flex-col">
                    <div className="flex items-center gap-1.5">
                      <span className="text-xs font-medium text-zinc-900 dark:text-zinc-100">
                        {t.settings.excludedPrograms}
                      </span>
                      <Badge
                        variant={excludedPrograms.length > 0 ? 'info' : 'default'}
                        className="!h-4 !rounded-[4px] !px-1.5 !py-0 font-mono !text-[10px] leading-4">
                        {excludedPrograms.length}
                      </Badge>
                    </div>
                    <span className="text-[11px] leading-snug text-zinc-500 dark:text-zinc-400">
                      {t.settings.excludedProgramsDesc}
                    </span>
                  </div>
                  <Button
                    variant="secondary"
                    size="sm"
                    onClick={() => {
                      const nextOpen = !showExclusionPanel
                      setShowExclusionPanel(nextOpen)
                      setSelectedConnectedAppId(null)
                      if (nextOpen) {
                        bridge.sendCommand('requestConnectedApplications')
                      }
                    }}
                    className="shrink-0">
                    <span>{t.settings.excludedProgramsManage}</span>
                    {showExclusionPanel ? <ChevronUp className="h-3 w-3" /> : <ChevronDown className="h-3 w-3" />}
                  </Button>
                </div>

                {showExclusionPanel && (
                  <div className="mt-2 flex flex-col gap-1.5 rounded-md border border-zinc-200/80 bg-zinc-50/70 p-2 dark:border-zinc-800/80 dark:bg-zinc-900/50">
                    {/* Excluded tags summary (only shown when expanded) */}
                    {excludedPrograms.length > 0 && (
                      <div className="flex flex-wrap gap-1 pb-0.5">
                        {excludedPrograms.map(prog => (
                          <span
                            key={prog}
                            className="inline-flex items-center gap-1 rounded-[4px] border border-zinc-200 bg-white px-1.5 py-0.5 font-mono text-[10px] text-zinc-800 dark:border-zinc-800 dark:bg-zinc-950 dark:text-zinc-200">
                            <span className="max-w-[180px] truncate" title={prog}>
                              {prog}
                            </span>
                            <button
                              type="button"
                              aria-label={`${t.common.close} ${prog}`}
                              onClick={() => handleRemoveExcludedProgram(prog)}
                              className="cursor-pointer rounded-xs p-0.5 text-zinc-400 hover:bg-zinc-100 hover:text-zinc-700 dark:hover:bg-zinc-800 dark:hover:text-zinc-200">
                              <X className="h-2.5 w-2.5" />
                            </button>
                          </span>
                        ))}
                      </div>
                    )}
                    {/* Unified Search + Manual Add Input + Add / Browse / Refresh Buttons */}
                    <div className="flex items-center gap-1">
                      <div className="relative flex min-w-0 flex-1 items-center">
                        <Search className="pointer-events-none absolute left-2 h-3 w-3 text-zinc-400" />
                        <input
                          type="text"
                          value={excludeFilterInput}
                          onChange={e => {
                            setExcludeFilterInput(e.target.value)
                            setSelectedConnectedAppId(null)
                          }}
                          onKeyDown={e => {
                            if (e.key === 'Enter') {
                              e.preventDefault()
                              if (pendingAddTarget) {
                                handleAddExcludedProgram(pendingAddTarget)
                                setSelectedConnectedAppId(null)
                              }
                            }
                          }}
                          placeholder={t.settings.excludedProgramsInputPlaceholder}
                          className="h-6 w-full rounded-[5px] border border-zinc-200 bg-white pr-6 pl-6 text-[11px] text-zinc-900 placeholder:text-zinc-400 focus:border-zinc-400 focus:outline-none dark:border-zinc-800 dark:bg-zinc-950 dark:text-zinc-100 dark:placeholder:text-zinc-500 dark:focus:border-zinc-600"
                        />
                        {excludeFilterInput && (
                          <button
                            type="button"
                            onClick={() => setExcludeFilterInput('')}
                            className="absolute right-1.5 cursor-pointer text-zinc-400 hover:text-zinc-600 dark:hover:text-zinc-200">
                            <X className="h-3 w-3" />
                          </button>
                        )}
                      </div>
                      <Button
                        variant={pendingAddTarget ? 'primary' : 'outline'}
                        size="sm"
                        disabled={!pendingAddTarget}
                        onClick={() => {
                          if (pendingAddTarget) {
                            handleAddExcludedProgram(pendingAddTarget)
                            setSelectedConnectedAppId(null)
                          }
                        }}
                        className="shrink-0">
                        <Plus className="h-3 w-3" />
                        <span>{t.settings.excludedProgramsAdd}</span>
                      </Button>
                      <Button
                        variant="outline"
                        size="sm"
                        onClick={() => bridge.sendCommand('browseExcludedProgram')}
                        className="shrink-0">
                        <FolderOpen className="h-3 w-3" />
                        <span>{t.settings.excludedProgramsBrowse}</span>
                      </Button>
                      <Button
                        variant="outline"
                        size="sm"
                        aria-label={t.settings.excludedProgramsRefresh}
                        title={t.settings.excludedProgramsRefresh}
                        onClick={() => bridge.sendCommand('requestConnectedApplications')}
                        className="shrink-0">
                        <RefreshCw className="h-3 w-3" />
                        <span>{t.settings.excludedProgramsRefresh}</span>
                      </Button>
                    </div>

                    {/* Connected Applications Compact Single-Line List (Click row to select, then click Add) */}
                    <div className="max-h-36 divide-y divide-zinc-100 overflow-y-auto rounded-[5px] border border-zinc-200/80 bg-white dark:divide-zinc-900 dark:border-zinc-800/80 dark:bg-zinc-950">
                      {filteredConnectedApps.length === 0 ? (
                        <div className="py-3 text-center text-[11px] text-zinc-400 dark:text-zinc-500">
                          {t.settings.excludedProgramsNoConnected}
                        </div>
                      ) : (
                        filteredConnectedApps.map(app => {
                          const isExcluded = isAppMatchingExcluded(app, excludedPrograms)
                          const isSelected = !isExcluded && selectedConnectedApp?.id === app.id
                          const targetName = app.executableName || app.displayName || app.id
                          return (
                            <button
                              key={app.id}
                              type="button"
                              disabled={isExcluded}
                              aria-pressed={isSelected}
                              onClick={() => {
                                if (!isExcluded) {
                                  setSelectedConnectedAppId(prev => (prev === app.id ? null : app.id))
                                }
                              }}
                              title={app.executablePath || targetName}
                              className={`flex h-6 w-full items-center justify-between gap-2 px-2 text-left transition-colors ${
                                isExcluded
                                  ? 'cursor-default bg-zinc-50/60 opacity-60 dark:bg-zinc-900/40'
                                  : isSelected
                                    ? 'cursor-pointer bg-zinc-900/10 ring-1 ring-zinc-900/20 ring-inset dark:bg-zinc-100/15 dark:ring-zinc-100/25'
                                    : 'cursor-pointer hover:bg-zinc-100/80 active:bg-zinc-200/60 dark:hover:bg-zinc-900 dark:active:bg-zinc-800/80'
                              }`}>
                              <div className="flex min-w-0 flex-1 items-center gap-1.5">
                                {app.iconKey && app.iconKey.startsWith('data:image/') ? (
                                  <img src={app.iconKey} alt="" className="h-3.5 w-3.5 shrink-0 object-contain" />
                                ) : (
                                  <AppWindow className="h-3.5 w-3.5 shrink-0 text-zinc-400" />
                                )}
                                <span className="shrink-0 text-[11px] font-medium text-zinc-900 dark:text-zinc-100">
                                  {app.displayName || targetName}
                                </span>
                                {app.executablePath && (
                                  <span className="min-w-0 flex-1 truncate font-mono text-[10px] text-zinc-400 dark:text-zinc-500">
                                    {app.executablePath}
                                  </span>
                                )}
                              </div>
                              {isExcluded ? (
                                <span className="inline-flex shrink-0 items-center gap-0.5 text-[10px] font-medium text-zinc-500 dark:text-zinc-400">
                                  <Check className="h-3 w-3" />
                                  <span>{t.settings.excludedProgramsAdded}</span>
                                </span>
                              ) : isSelected ? (
                                <Check className="h-3 w-3 shrink-0 text-zinc-900 dark:text-zinc-100" />
                              ) : null}
                            </button>
                          )
                        })
                      )}
                    </div>
                  </div>
                )}
              </div>
              <SettingsRow
                label={t.settings.appVersion}
                description={
                  updateStatus.state === 'Error' && updateStatus.errorMessage
                    ? `${t.settings.updateFailed}: ${updateStatus.errorMessage}`
                    : t.settings.appVersionDesc
                }
                control={
                  <div className="flex items-center gap-2">
                    <Badge variant="default" className="font-mono">
                      v{updateStatus.currentVersion || '0.3.0'}
                    </Badge>
                    {updateStatus.state === 'UpToDate' && <Badge variant="success">{t.settings.updateUpToDate}</Badge>}
                    {updateStatus.state === 'UpdateAvailable' && updateStatus.latestVersion && (
                      <Badge variant="info">
                        {t.settings.updateAvailable}: v{updateStatus.latestVersion}
                      </Badge>
                    )}
                    {updateStatus.state === 'Error' && <Badge variant="danger">{t.settings.updateFailed}</Badge>}
                    {updateStatus.state === 'UpdateAvailable' ? (
                      <Button variant="primary" size="sm" onClick={() => bridge.sendCommand('downloadAndApplyUpdate')}>
                        <Download className="h-3 w-3" />
                        {t.settings.updateNowAndRestart}
                      </Button>
                    ) : updateStatus.state === 'Downloading' ? (
                      <Button variant="secondary" size="sm" disabled>
                        <RefreshCw className="h-3 w-3 animate-spin" />
                        {t.settings.downloadingUpdate} ({updateStatus.downloadProgressPercent}%)
                      </Button>
                    ) : updateStatus.state === 'Applying' ? (
                      <Button variant="secondary" size="sm" disabled>
                        <RefreshCw className="h-3 w-3 animate-spin" />
                        {t.settings.applyingUpdate}
                      </Button>
                    ) : (
                      <Button
                        variant="primary"
                        size="sm"
                        disabled={updateStatus.state === 'Checking'}
                        onClick={() => bridge.sendCommand('checkForUpdate')}>
                        <RefreshCw className={`h-3 w-3 ${updateStatus.state === 'Checking' ? 'animate-spin' : ''}`} />
                        {updateStatus.state === 'Checking' ? t.settings.checkingUpdates : t.settings.checkForUpdates}
                      </Button>
                    )}
                  </div>
                }
              />
            </Card>
          )}

          {/* 2. Capsule Tab */}
          {activeTab === 'capsule' && (
            <Card className="flex flex-col divide-y divide-zinc-200/70 px-3 py-2 dark:divide-zinc-800/80">
              <SettingsRow
                label={t.settings.capsuleColors}
                description={t.settings.capsuleColorsDesc}
                control={
                  <div className="flex items-center gap-2">
                    {/* Modern Dual-Tone Pill Preset Bar */}
                    <div
                      className="flex items-center gap-1 rounded-md border border-zinc-200 bg-zinc-100/70 p-0.5 dark:border-zinc-800 dark:bg-zinc-900/80"
                      role="group"
                      aria-label={t.settings.colorPresets}>
                      {CAPSULE_COLOR_PRESETS.map(preset => {
                        const isSelected =
                          downloadColor === preset.downloadHex.toLowerCase() &&
                          uploadColor === preset.uploadHex.toLowerCase()
                        return (
                          <button
                            key={preset.id}
                            type="button"
                            onClick={() => {
                              handleUpdate('floating.download_color_hex', preset.downloadHex)
                              handleUpdate('floating.upload_color_hex', preset.uploadHex)
                            }}
                            title={`${preset.downloadHex} / ${preset.uploadHex}`}
                            className={`group relative flex h-5 w-7 cursor-pointer items-center justify-center rounded-[5px] p-[2px] transition-all ${
                              isSelected
                                ? 'bg-white shadow-xs ring-1 ring-zinc-900 dark:bg-zinc-950 dark:ring-zinc-100'
                                : 'opacity-75 hover:bg-white/60 hover:opacity-100 dark:hover:bg-zinc-800/60'
                            }`}>
                            <span
                              className="block h-full w-full rounded-[3px] ring-1 ring-black/10 ring-inset dark:ring-white/10"
                              style={{
                                background: `linear-gradient(135deg, ${preset.downloadHex} 0%, ${preset.downloadHex} 50%, ${preset.uploadHex} 50%, ${preset.uploadHex} 100%)`,
                              }}
                            />
                          </button>
                        )
                      })}
                    </div>

                    {/* Custom Popover Color Pickers */}
                    <div className="flex items-center gap-1.5">
                      <ColorPicker
                        prefix="↓"
                        label={t.settings.downloadColor}
                        ariaLabel={t.settings.downloadColor}
                        value={downloadColor}
                        onChange={hex => handleUpdate('floating.download_color_hex', hex)}
                      />
                      <ColorPicker
                        prefix="↑"
                        label={t.settings.uploadColor}
                        ariaLabel={t.settings.uploadColor}
                        value={uploadColor}
                        onChange={hex => handleUpdate('floating.upload_color_hex', hex)}
                      />
                    </div>
                  </div>
                }
              />
              <SettingsRow
                label={t.settings.capsuleWidth}
                description={t.settings.capsuleWidthDesc}
                control={
                  <Slider
                    value={config.capsule.widthDip}
                    min={schema.capsuleWidth.min}
                    max={schema.capsuleWidth.max}
                    unit={schema.capsuleWidth.unit}
                    onChange={val => handleUpdate('floating.capsule_width_dip', val)}
                    ariaLabel={t.settings.capsuleWidth}
                  />
                }
              />
              <SettingsRow
                label={t.settings.capsuleHeight}
                description={t.settings.capsuleHeightDesc}
                control={
                  <Slider
                    value={config.capsule.heightDip}
                    min={schema.capsuleHeight.min}
                    max={schema.capsuleHeight.max}
                    unit={schema.capsuleHeight.unit}
                    onChange={val => handleUpdate('floating.capsule_height_dip', val)}
                    ariaLabel={t.settings.capsuleHeight}
                  />
                }
              />
              <SettingsRow
                label={t.settings.capsuleEndRounding}
                description={`${t.settings.capsuleEndRoundingDesc} (${t.settings.effectiveRoundingNote}: ${config.capsule.effectiveEndRoundingDip} DIP)`}
                control={
                  <Slider
                    value={config.capsule.endRoundingDip}
                    min={schema.capsuleEndRounding.min}
                    max={schema.capsuleEndRounding.max}
                    unit={schema.capsuleEndRounding.unit}
                    onChange={val => handleUpdate('floating.capsule_end_rounding_dip', val)}
                    ariaLabel={t.settings.capsuleEndRounding}
                  />
                }
              />
              <SettingsRow
                label={t.settings.hoverDelay}
                description={t.settings.hoverDelayDesc}
                control={
                  <Slider
                    value={config.capsule.hoverDelayMs}
                    min={schema.hoverDelay.min}
                    max={schema.hoverDelay.max}
                    step={schema.hoverDelay.step}
                    unit={schema.hoverDelay.unit}
                    onChange={val => handleUpdate('interaction.hover_delay_ms', val)}
                    ariaLabel={t.settings.hoverDelay}
                  />
                }
              />
              <SettingsRow
                label={t.settings.dismissDelay}
                description={t.settings.dismissDelayDesc}
                control={
                  <Slider
                    value={config.monitor.dismissDelayMs}
                    min={schema.dismissDelay.min}
                    max={schema.dismissDelay.max}
                    step={schema.dismissDelay.step}
                    unit={schema.dismissDelay.unit}
                    onChange={val => handleUpdate('interaction.dismiss_delay_ms', val)}
                    ariaLabel={t.settings.dismissDelay}
                  />
                }
              />
              <SettingsRow
                label={t.settings.autoHide}
                description={t.settings.autoHideDesc}
                control={
                  <Toggle
                    checked={config.capsule.autoHideEnabled}
                    onChange={val => handleUpdate('floating.auto_hide_enabled', val)}
                    ariaLabel={t.settings.autoHide}
                  />
                }
              />
              <SettingsRow
                label={t.settings.ignoreHoverFullscreen}
                description={t.settings.ignoreHoverFullscreenDesc}
                control={
                  <Toggle
                    checked={config.capsule.ignoreHoverWhenFullscreen ?? true}
                    onChange={val => handleUpdate('floating.ignore_hover_when_fullscreen', val)}
                    ariaLabel={t.settings.ignoreHoverFullscreen}
                  />
                }
              />
              <SettingsRow
                label={t.settings.hideCapsuleFullscreen}
                description={t.settings.hideCapsuleFullscreenDesc}
                control={
                  <Toggle
                    checked={config.capsule.hideCapsuleWhenFullscreen ?? false}
                    onChange={val => handleUpdate('floating.hide_capsule_when_fullscreen', val)}
                    ariaLabel={t.settings.hideCapsuleFullscreen}
                  />
                }
              />
            </Card>
          )}

          {/* 3. Traffic History Tab */}
          {activeTab === 'history' && (
            <TrafficHistoryPanel
              history={trafficHistory}
              downloadColorHex={downloadColor}
              uploadColorHex={uploadColor}
              onRefresh={() => bridge.sendCommand('requestTrafficHistory')}
              onLayoutChange={() => setHistoryLayoutSeq(s => s + 1)}
            />
          )}
          {/* 3. Collector & Diagnostics Tab */}
          {activeTab === 'collector' && (
            <Card className="flex flex-col divide-y divide-zinc-200/70 px-3 py-2 dark:divide-zinc-800/80">
              <div className="flex items-center justify-between gap-3 py-1.5 first:pt-0.5">
                <div className="flex items-center gap-2">
                  <span className="text-xs font-medium text-zinc-900 dark:text-zinc-100">
                    {t.settings.collectorStatus}
                  </span>
                  <Badge
                    variant={
                      collectorStatus.state === 'Running'
                        ? 'success'
                        : collectorStatus.state === 'Starting'
                          ? 'warning'
                          : 'danger'
                    }>
                    {collectorStatus.state === 'Running'
                      ? t.settings.statusRunning
                      : collectorStatus.state === 'Starting'
                        ? t.settings.statusStarting
                        : collectorStatus.state === 'Stopped'
                          ? t.settings.statusStopped
                          : t.settings.statusFailed}
                  </Badge>
                </div>
                <div className="flex items-center gap-1.5">
                  <Button variant="primary" size="sm" onClick={() => bridge.sendCommand('restartCollector')}>
                    <RotateCw className="h-3 w-3" />
                    <span>
                      {collectorStatus.state === 'Running' && collectorStatus.etwSessionActive
                        ? t.settings.restartCollector
                        : t.settings.retryCollector}
                    </span>
                  </Button>
                </div>
              </div>

              <SettingsRow
                label={t.settings.logLevel}
                description={t.settings.logLevelDesc}
                control={
                  <div className="flex items-center gap-1.5">
                    <Select
                      value={config.diagnostics.logLevel}
                      onChange={val => handleUpdate('diagnostics.log_level', val)}
                      options={[
                        { value: 'off', label: t.settings.logLevelOff },
                        { value: 'error', label: t.settings.logLevelError },
                        { value: 'info', label: t.settings.logLevelInfo },
                        { value: 'debug', label: t.settings.logLevelDebug },
                      ]}
                    />
                    <Button variant="primary" size="sm" onClick={() => bridge.sendCommand('openLogDirectory')}>
                      <FolderOpen className="h-3 w-3" />
                      <span>{t.settings.openLogDirectory}</span>
                    </Button>
                  </div>
                }
              />
              <SettingsRow
                label={t.settings.processId}
                control={
                  <span className="font-mono text-xs font-medium text-zinc-900 tabular-nums dark:text-zinc-100">
                    {collectorStatus.processId ? collectorStatus.processId : t.settings.none}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.activeProcesses}
                control={
                  <span className="font-mono text-xs font-medium text-zinc-900 tabular-nums dark:text-zinc-100">
                    {collectorStatus.activePids}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.droppedPackets}
                control={
                  <span className="font-mono text-xs font-medium text-zinc-900 tabular-nums dark:text-zinc-100">
                    {collectorStatus.droppedPackets}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.etwHealth}
                control={
                  <Badge variant={collectorStatus.etwSessionActive ? 'success' : 'danger'}>
                    {collectorStatus.etwSessionActive ? t.settings.etwActive : t.settings.etwInactive}
                  </Badge>
                }
              />
              <SettingsRow
                label={t.settings.elevation}
                control={
                  <Badge variant={collectorStatus.isElevated ? 'default' : 'warning'}>
                    {collectorStatus.isElevated ? t.settings.elevationYes : t.settings.elevationNo}
                  </Badge>
                }
              />
              {collectorStatus.lastError && (
                <SettingsRow
                  label={t.settings.lastError}
                  control={
                    <span className="font-mono text-xs text-red-600 dark:text-red-400">
                      {collectorStatus.lastError}
                    </span>
                  }
                />
              )}
              <SettingsRow
                label={t.settings.webviewRuntime}
                control={<Badge variant="success">{t.settings.stateOk}</Badge>}
              />
              <SettingsRow
                label={t.settings.assetsState}
                control={<Badge variant="success">{t.settings.stateOk}</Badge>}
              />
              <SettingsRow
                label={t.settings.bridgeProtocol}
                control={<Badge variant="success">{t.settings.stateOk}</Badge>}
              />
            </Card>
          )}
        </div>
      </div>
    </div>
  )
}
