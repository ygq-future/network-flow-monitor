import { BRIDGE_PROTOCOL_VERSION } from '../shared/constants/protocol'

export type WebCommandType =
  | 'showMonitor'
  | 'hideMonitor'
  | 'setPin'
  | 'openSettings'
  | 'requestConfig'
  | 'updateSetting'
  | 'retryCollector'
  | 'restartCollector'
  | 'minimizeWindow'
  | 'toggleMaximize'
  | 'toggleMaximizeWindow'
  | 'startWindowDrag'
  | 'exitApp'
  | 'setTheme'
  | 'setLocale'
  | 'openContextMenu'
  | 'resizePanel'
  | 'resizeSettingsWindow'
  | 'openLogDirectory'
export type NativeEventType =
  | 'trafficTick'
  | 'rankingSnapshot'
  | 'collectorStatus'
  | 'sessionReset'
  | 'themeChanged'
  | 'localeChanged'
  | 'configChanged'
  | 'surfaceChanged'
  | 'commandResponse'
  | 'error'

export interface TrafficTickDto {
  seq: number
  monotonicMs: number
  downloadBytesPerSec: number
  uploadBytesPerSec: number
}

export interface RankingAppDto {
  id: string
  executablePath: string
  displayName: string
  iconKey: string
  downloadBytesPerSec: number
  uploadBytesPerSec: number
}

export interface RankingSnapshotDto {
  seq: number
  monotonicMs: number
  status: string
  totalDownloadBytesPerSec: number
  totalUploadBytesPerSec: number
  applications: RankingAppDto[]
}

export interface CollectorStatusDto {
  monotonicMs: number
  etwSessionActive: boolean
  droppedPackets: number
  activePids: number
  state?: 'Stopped' | 'Starting' | 'Running' | 'Failed'
  processId?: number
  instanceId?: string
  isElevated?: boolean
  lastError?: string
}

export interface SessionResetDto {
  collectorInstanceId: string
  collectorPid: number
}

export interface ThemeChangedDto {
  theme: 'System' | 'Light' | 'Dark'
}

export interface LocaleChangedDto {
  locale: 'System' | 'English' | 'SimplifiedChinese'
}

export interface NumericRangeSchema {
  min: number
  max: number
  step?: number
  unit: string
  default: number
}

export interface ConfigSchemaDto {
  capsuleWidth: NumericRangeSchema
  capsuleHeight: NumericRangeSchema
  capsuleEndRounding: NumericRangeSchema
  hoverDelay: NumericRangeSchema
  autoHideDelay: NumericRangeSchema
  dismissDelay: NumericRangeSchema
  opacity: NumericRangeSchema
  blurStrength: NumericRangeSchema
  topN: NumericRangeSchema
  rankingWindow: NumericRangeSchema
  refreshInterval: NumericRangeSchema
  speedUnits: ('auto' | 'bytes' | 'bits')[]
  themes: ('system' | 'light' | 'dark')[]
  languages: ('system' | 'english' | 'simplified_chinese')[]
  logLevels: ('off' | 'error' | 'info' | 'debug')[]
  dockSides: ('left' | 'right')[]
}

export interface GeneralConfigDto {
  uiAtLogin: boolean
  speedUnit: 'auto' | 'bytes' | 'bits'
}

export interface AppearanceConfigDto {
  theme: 'system' | 'light' | 'dark'
  language: 'system' | 'english' | 'simplified_chinese'
}

export interface CapsuleConfigDto {
  widthDip: number
  heightDip: number
  endRoundingDip: number
  effectiveEndRoundingDip: number
  hoverDelayMs: number
  anchorSide: 'left' | 'right'
  anchorMonitorId: string
  anchorVerticalOffsetDip: number
  autoHideEnabled: boolean
  autoHideDelayMs: number
  downloadColorHex?: string
  uploadColorHex?: string
  showCapsuleBorder?: boolean
  ignoreHoverWhenFullscreen?: boolean
  hideCapsuleWhenFullscreen?: boolean
}

export interface MonitorConfigDto {
  pinned: boolean
  dismissDelayMs: number
  opacityPercent: number
  blurStrengthPercent: number
}

export interface RankingConfigDto {
  topN: number
  windowMs: number
  refreshIntervalMs: number
}

export interface DiagnosticsConfigDto {
  logLevel: 'off' | 'error' | 'info' | 'debug'
}

export interface AppConfigDto {
  general: GeneralConfigDto
  appearance: AppearanceConfigDto
  capsule: CapsuleConfigDto
  monitor: MonitorConfigDto
  ranking: RankingConfigDto
  diagnostics: DiagnosticsConfigDto
}

export interface ConfigChangedDto {
  config: AppConfigDto
  schema: ConfigSchemaDto
}

export interface SurfaceChangedDto {
  surface: 'tray' | 'capsule' | 'panel' | 'settings'
  pinned: boolean
  dockSide: 'left' | 'right'
  autoHidden?: boolean
}

export interface UpdateSettingPayload {
  key: string
  value: unknown
}

export interface CommandResponseDto {
  commandId: string
  success: boolean
  message?: string
}

export interface BridgeErrorDto {
  code: string
  message: string
}

export interface WebCommandEnvelope<T = unknown> {
  type: WebCommandType
  version?: number
  commandId?: string
  payload?: T
}

export interface NativeEventEnvelope<T = unknown> {
  type: NativeEventType
  version: number
  payload: T
}

export function createWebCommand<T>(type: WebCommandType, payload?: T, commandId?: string): string {
  const envelope: WebCommandEnvelope<T> = {
    type,
    version: BRIDGE_PROTOCOL_VERSION,
    commandId: commandId ?? `cmd_${Date.now()}_${Math.random().toString(36).slice(2, 7)}`,
    payload,
  }
  return JSON.stringify(envelope)
}
