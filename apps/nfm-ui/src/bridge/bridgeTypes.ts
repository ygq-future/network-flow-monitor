import { BRIDGE_PROTOCOL_VERSION } from '../shared/constants/protocol'

export type WebCommandType =
  | 'showMonitor'
  | 'hideMonitor'
  | 'setPin'
  | 'openSettings'
  | 'updateSetting'
  | 'retryCollector'
  | 'exitApp'
  | 'setTheme'
  | 'setLocale'

export type NativeEventType =
  | 'trafficTick'
  | 'rankingSnapshot'
  | 'collectorStatus'
  | 'sessionReset'
  | 'themeChanged'
  | 'localeChanged'
  | 'configChanged'
  | 'commandResponse'
  | 'error'

export interface TrafficTickDto {
  seq: number
  monotonicMs: number
  downloadBytesPerSec: number
  uploadBytesPerSec: number
}

export interface CollectorStatusDto {
  monotonicMs: number
  etwSessionActive: boolean
  droppedPackets: number
  activePids: number
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
