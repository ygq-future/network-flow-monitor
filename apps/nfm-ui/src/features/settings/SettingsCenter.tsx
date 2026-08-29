import React, { useState, useEffect } from 'react'
import {
  Sliders,
  Palette,
  Layers,
  Monitor,
  BarChart2,
  Activity,
  FileText,
  ArrowLeft,
  RefreshCw,
  RotateCw,
  CheckCircle2,
  AlertCircle,
  ShieldCheck,
  ShieldAlert,
} from 'lucide-react'
import { useBridge } from '../../bridge/BridgeContext'
import {
  AppConfigDto,
  ConfigSchemaDto,
  ConfigChangedDto,
  CollectorStatusDto,
  BridgeErrorDto,
} from '../../bridge/bridgeTypes'
import { defaultMockConfigChanged } from '../../bridge/nativeBridge'
import { useTranslation } from '../../shared/i18n/I18nContext'
import { useTheme } from '../../shared/theme/ThemeContext'
import { Button } from '../../shared/components/Button'
import { IconButton } from '../../shared/components/IconButton'
import { Select } from '../../shared/components/Select'
import { Slider } from '../../shared/components/Slider'
import { Toggle } from '../../shared/components/Toggle'
import { Badge } from '../../shared/components/Badge'
import { SettingsSection } from '../../shared/components/SettingsSection'
import { SettingsRow } from '../../shared/components/SettingsRow'

export type SettingsTab = 'general' | 'appearance' | 'capsule' | 'monitor' | 'ranking' | 'collector' | 'diagnostics'

export interface SettingsCenterProps {
  initialTab?: SettingsTab
  onBack?: () => void
}

export const SettingsCenter: React.FC<SettingsCenterProps> = ({ initialTab = 'general', onBack }) => {
  const { t, setLocale } = useTranslation()
  const { setTheme } = useTheme()
  const bridge = useBridge()

  const [activeTab, setActiveTab] = useState<SettingsTab>(initialTab)
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
  const [errorMessage, setErrorMessage] = useState<string | null>(null)
  const [prefersReducedMotion, setPrefersReducedMotion] = useState(false)

  // Detect prefers-reduced-motion
  useEffect(() => {
    if (typeof window !== 'undefined' && window.matchMedia) {
      const mql = window.matchMedia('(prefers-reduced-motion: reduce)')
      setPrefersReducedMotion(mql.matches)
      const handler = (e: MediaQueryListEvent) => setPrefersReducedMotion(e.matches)
      mql.addEventListener('change', handler)
      return () => mql.removeEventListener('change', handler)
    }
  }, [])

  // Listen to bridge events
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

    const unsubError = bridge.on<BridgeErrorDto>('error', err => {
      if (err) {
        setErrorMessage(`${err.code}: ${err.message}`)
      }
    })

    // Request authoritative config
    bridge.sendCommand('requestConfig')

    return () => {
      unsubConfig()
      unsubCollector()
      unsubError()
    }
  }, [bridge])

  const handleUpdate = (key: string, value: unknown) => {
    bridge.sendCommand('updateSetting', { key, value })
  }

  const tabs: { id: SettingsTab; label: string; icon: React.ComponentType<{ className?: string }> }[] = [
    { id: 'general', label: t.settings.general, icon: Sliders },
    { id: 'appearance', label: t.settings.appearance, icon: Palette },
    { id: 'capsule', label: t.settings.capsule, icon: Layers },
    { id: 'monitor', label: t.settings.monitor, icon: Monitor },
    { id: 'ranking', label: t.settings.ranking, icon: BarChart2 },
    { id: 'collector', label: t.settings.collector, icon: Activity },
    { id: 'diagnostics', label: t.settings.diagnostics, icon: FileText },
  ]

  return (
    <div className="mx-auto flex max-w-4xl flex-col gap-4 p-4 pb-20 select-none">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          {onBack && (
            <IconButton aria-label={t.settings.back} variant="subtle" size="md" onClick={onBack}>
              <ArrowLeft className="h-4 w-4" />
            </IconButton>
          )}
          <div className="flex flex-col">
            <h1 className="text-base font-bold text-slate-900 dark:text-slate-100">{t.settings.title}</h1>
            <p className="text-xs text-slate-500 dark:text-slate-400">
              {tabs.find(x => x.id === activeTab)?.label} — {t.common.appName}
            </p>
          </div>
        </div>

        {errorMessage && (
          <div className="flex items-center gap-1.5 rounded-lg border border-red-200 bg-red-50 px-3 py-1.5 text-xs font-medium text-red-700 dark:border-red-900/50 dark:bg-red-950/40 dark:text-red-300">
            <AlertCircle className="h-4 w-4 shrink-0" />
            <span>{errorMessage}</span>
          </div>
        )}
      </div>

      {/* Main Settings Body with Sidebar Tabs */}
      <div className="flex flex-col items-start gap-4 md:flex-row">
        {/* Navigation Sidebar */}
        <nav className="flex w-full shrink-0 flex-col gap-1 rounded-xl border border-slate-200/80 bg-white/70 p-1.5 shadow-sm md:sticky md:top-0 md:w-48 dark:border-slate-800 dark:bg-slate-900/70">
          {tabs.map(tab => {
            const Icon = tab.icon
            const isActive = activeTab === tab.id
            return (
              <button
                key={tab.id}
                onClick={() => setActiveTab(tab.id)}
                className={`flex cursor-pointer items-center gap-2.5 rounded-lg px-3 py-2 text-left text-xs font-medium whitespace-nowrap transition-colors ${
                  isActive
                    ? 'bg-blue-600 text-white shadow-xs dark:bg-blue-600'
                    : 'text-slate-700 hover:bg-slate-100/80 dark:text-slate-300 dark:hover:bg-slate-800/80'
                }`}>
                <Icon
                  className={`h-4 w-4 shrink-0 ${isActive ? 'text-white' : 'text-slate-500 dark:text-slate-400'}`}
                />
                <span>{tab.label}</span>
              </button>
            )
          })}
        </nav>

        {/* Content Pane */}
        <div className="flex min-w-0 flex-1 flex-col gap-4">
          {/* 1. General Tab */}
          {activeTab === 'general' && (
            <SettingsSection title={t.settings.general} description={t.settings.generalDesc} icon={Sliders}>
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
            </SettingsSection>
          )}

          {/* 2. Appearance Tab */}
          {activeTab === 'appearance' && (
            <SettingsSection title={t.settings.appearance} description={t.settings.appearanceDesc} icon={Palette}>
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
                label={t.settings.reducedMotion}
                description={t.settings.reducedMotionDesc}
                control={
                  <Badge variant={prefersReducedMotion ? 'success' : 'default'}>
                    {prefersReducedMotion ? t.settings.reducedMotionActive : t.settings.reducedMotionInactive}
                  </Badge>
                }
              />
            </SettingsSection>
          )}

          {/* 3. Capsule Tab */}
          {activeTab === 'capsule' && (
            <SettingsSection title={t.settings.capsule} description={t.settings.capsuleDesc} icon={Layers}>
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
                label={t.settings.anchorSide}
                description={t.settings.anchorSideDesc}
                control={
                  <Select
                    value={config.capsule.anchorSide}
                    onChange={val => handleUpdate('floating.anchor_side', val)}
                    options={[
                      { value: 'right', label: t.settings.sideRight },
                      { value: 'left', label: t.settings.sideLeft },
                    ]}
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
              {config.capsule.autoHideEnabled && (
                <SettingsRow
                  label={t.settings.autoHideDelay}
                  description={t.settings.autoHideDelayDesc}
                  control={
                    <Slider
                      value={config.capsule.autoHideDelayMs}
                      min={schema.autoHideDelay.min}
                      max={schema.autoHideDelay.max}
                      step={schema.autoHideDelay.step}
                      unit={schema.autoHideDelay.unit}
                      onChange={val => handleUpdate('floating.auto_hide_delay_ms', val)}
                      ariaLabel={t.settings.autoHideDelay}
                    />
                  }
                />
              )}
            </SettingsSection>
          )}

          {/* 4. Monitor Tab */}
          {activeTab === 'monitor' && (
            <SettingsSection title={t.settings.monitor} description={t.settings.monitorDesc} icon={Monitor}>
              <SettingsRow
                label={t.settings.pinned}
                description={t.settings.pinnedDesc}
                control={
                  <Toggle
                    checked={config.monitor.pinned}
                    onChange={val => handleUpdate('floating.nfm_pinned', val)}
                    ariaLabel={t.settings.pinned}
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
                label={t.settings.opacity}
                description={t.settings.opacityDesc}
                control={
                  <Slider
                    value={config.monitor.opacityPercent}
                    min={schema.opacity.min}
                    max={schema.opacity.max}
                    unit={schema.opacity.unit}
                    onChange={val => handleUpdate('floating.opacity_percent', val)}
                    ariaLabel={t.settings.opacity}
                  />
                }
              />
              <SettingsRow
                label={t.settings.blurStrength}
                description={t.settings.blurStrengthDesc}
                control={
                  <Slider
                    value={config.monitor.blurStrengthPercent}
                    min={schema.blurStrength.min}
                    max={schema.blurStrength.max}
                    unit={schema.blurStrength.unit}
                    onChange={val => handleUpdate('floating.blur_strength_percent', val)}
                    ariaLabel={t.settings.blurStrength}
                  />
                }
              />
              <SettingsRow
                label={t.settings.yieldTopmost}
                description={t.settings.yieldTopmostDesc}
                control={
                  <Toggle
                    checked={config.monitor.yieldTopmostWhenFullscreen}
                    onChange={val => handleUpdate('floating.yield_topmost_when_fullscreen', val)}
                    ariaLabel={t.settings.yieldTopmost}
                  />
                }
              />
            </SettingsSection>
          )}

          {/* 5. Ranking Tab */}
          {activeTab === 'ranking' && (
            <SettingsSection title={t.settings.ranking} description={t.settings.rankingDesc} icon={BarChart2}>
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
            </SettingsSection>
          )}

          {/* 6. Collector Tab */}
          {activeTab === 'collector' && (
            <SettingsSection
              title={t.settings.collector}
              description={t.settings.collectorDesc}
              icon={Activity}
              actions={
                <div className="flex items-center gap-2">
                  <Button
                    variant="outline"
                    size="sm"
                    onClick={() => bridge.sendCommand('retryCollector')}
                    className="gap-1.5">
                    <RefreshCw className="h-3.5 w-3.5" />
                    <span>{t.settings.retryCollector}</span>
                  </Button>
                  <Button
                    variant="primary"
                    size="sm"
                    onClick={() => bridge.sendCommand('restartCollector')}
                    className="gap-1.5">
                    <RotateCw className="h-3.5 w-3.5" />
                    <span>{t.settings.restartCollector}</span>
                  </Button>
                </div>
              }>
              <SettingsRow
                label={t.settings.collectorStatus}
                description={t.settings.collectorStatusDesc}
                control={
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
                }
              />
              <SettingsRow
                label={t.settings.processId}
                control={
                  <span className="font-mono text-xs font-semibold text-slate-700 dark:text-slate-300">
                    {collectorStatus.processId ? collectorStatus.processId : t.settings.none}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.instanceId}
                control={
                  <span className="font-mono text-xs text-slate-600 dark:text-slate-400">
                    {collectorStatus.instanceId ? collectorStatus.instanceId : t.settings.none}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.elevation}
                control={
                  <div className="flex items-center gap-1.5">
                    {collectorStatus.isElevated ? (
                      <>
                        <ShieldCheck className="h-4 w-4 text-emerald-600 dark:text-emerald-400" />
                        <span className="text-xs font-medium text-emerald-600 dark:text-emerald-400">
                          {t.settings.elevationYes}
                        </span>
                      </>
                    ) : (
                      <>
                        <ShieldAlert className="h-4 w-4 text-amber-600 dark:text-amber-400" />
                        <span className="text-xs font-medium text-amber-600 dark:text-amber-400">
                          {t.settings.elevationNo}
                        </span>
                      </>
                    )}
                  </div>
                }
              />
              <SettingsRow
                label={t.settings.etwHealth}
                control={
                  <div className="flex items-center gap-1.5">
                    {collectorStatus.etwSessionActive ? (
                      <>
                        <CheckCircle2 className="h-4 w-4 text-emerald-600 dark:text-emerald-400" />
                        <span className="text-xs font-medium text-emerald-600 dark:text-emerald-400">
                          {t.settings.etwActive}
                        </span>
                      </>
                    ) : (
                      <>
                        <AlertCircle className="h-4 w-4 text-red-600 dark:text-red-400" />
                        <span className="text-xs font-medium text-red-600 dark:text-red-400">
                          {t.settings.etwInactive}
                        </span>
                      </>
                    )}
                  </div>
                }
              />
              <SettingsRow
                label={t.settings.droppedPackets}
                control={
                  <span className="font-mono text-xs font-medium text-slate-700 dark:text-slate-300">
                    {collectorStatus.droppedPackets}
                  </span>
                }
              />
              <SettingsRow
                label={t.settings.activeProcesses}
                control={
                  <span className="font-mono text-xs font-medium text-slate-700 dark:text-slate-300">
                    {collectorStatus.activePids}
                  </span>
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
            </SettingsSection>
          )}

          {/* 7. Diagnostics Tab */}
          {activeTab === 'diagnostics' && (
            <>
              <SettingsSection title={t.settings.diagnostics} description={t.settings.diagnosticsDesc} icon={FileText}>
                <SettingsRow
                  label={t.settings.logLevel}
                  description={t.settings.logLevelDesc}
                  control={
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
                  }
                />
              </SettingsSection>
              <SettingsSection
                title={t.settings.systemDiagnostics}
                description={t.settings.systemDiagnosticsDesc}
                icon={CheckCircle2}>
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
              </SettingsSection>
            </>
          )}
        </div>
      </div>
    </div>
  )
}
