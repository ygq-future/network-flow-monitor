import {
  NativeEventType,
  WebCommandType,
  NativeEventEnvelope,
  createWebCommand,
  TrafficTickDto,
  RankingSnapshotDto,
  CollectorStatusDto,
  SessionResetDto,
  ConfigChangedDto,
  UpdateSettingPayload,
  SurfaceChangedDto,
  UpdateStatusDto,
  ConnectedApplicationsDto,
} from './bridgeTypes'

declare global {
  interface Window {
    chrome?: {
      webview?: {
        postMessage: (message: string | object) => void
        addEventListener: (type: 'message', listener: (event: { data: string | object }) => void) => void
        removeEventListener: (type: 'message', listener: (event: { data: string | object }) => void) => void
      }
    }
  }
}

export type EventHandler<T> = (data: T) => void

export interface INativeBridge {
  isWebView2: boolean
  sendCommand<T = unknown>(type: WebCommandType, payload?: T, commandId?: string): void
  on<T = unknown>(type: NativeEventType, handler: EventHandler<T>): () => void
  emit<T = unknown>(type: NativeEventType, payload: T): void
  destroy(): void
}

export class NativeBridge implements INativeBridge {
  public readonly isWebView2: boolean
  private listeners: Map<NativeEventType, Set<EventHandler<unknown>>> = new Map()
  private nativeMessageListener: ((event: { data: string | object }) => void) | null = null

  constructor() {
    this.isWebView2 = Boolean(typeof window !== 'undefined' && window.chrome?.webview?.postMessage)
    if (this.isWebView2 && window.chrome?.webview) {
      this.nativeMessageListener = (event: { data: string | object }) => {
        try {
          const raw = typeof event.data === 'string' ? JSON.parse(event.data) : event.data
          const envelope = raw as NativeEventEnvelope
          if (envelope && envelope.type) {
            this.emit(envelope.type, envelope.payload)
          }
        } catch {
          // Ignore malformed incoming webview message
        }
      }
      window.chrome.webview.addEventListener('message', this.nativeMessageListener)
    }
  }

  public sendCommand<T = unknown>(type: WebCommandType, payload?: T, commandId?: string): void {
    const json = createWebCommand(type, payload, commandId)
    if (this.isWebView2 && window.chrome?.webview?.postMessage) {
      window.chrome.webview.postMessage(json)
    }
  }

  public on<T = unknown>(type: NativeEventType, handler: EventHandler<T>): () => void {
    let set = this.listeners.get(type)
    if (!set) {
      set = new Set()
      this.listeners.set(type, set)
    }
    set.add(handler as EventHandler<unknown>)
    return () => {
      set?.delete(handler as EventHandler<unknown>)
    }
  }

  public emit<T = unknown>(type: NativeEventType, payload: T): void {
    const set = this.listeners.get(type)
    if (set) {
      for (const handler of set) {
        try {
          handler(payload)
        } catch (e) {
          console.error(`Error in bridge listener for ${type}:`, e)
        }
      }
    }
  }

  public destroy(): void {
    if (this.nativeMessageListener && window.chrome?.webview) {
      window.chrome.webview.removeEventListener('message', this.nativeMessageListener)
      this.nativeMessageListener = null
    }
    this.listeners.clear()
  }
}

export const defaultMockConfigChanged: ConfigChangedDto = {
  config: {
    general: {
      uiAtLogin: false,
      speedUnit: 'auto',
    },
    appearance: {
      theme: 'system',
      language: 'english',
    },
    capsule: {
      widthDip: 10,
      heightDip: 64,
      endRoundingDip: 5,
      effectiveEndRoundingDip: 5,
      hoverDelayMs: 350,
      anchorSide: 'right',
      anchorMonitorId: '',
      anchorVerticalOffsetDip: 64,
      autoHideEnabled: false,
      autoHideDelayMs: 500,
      downloadColorHex: '#38bdf8',
      uploadColorHex: '#2dd4bf',
      showCapsuleBorder: false,
      ignoreHoverWhenFullscreen: true,
      hideCapsuleWhenFullscreen: false,
    },
    monitor: {
      pinned: false,
      dismissDelayMs: 500,
      opacityPercent: 100,
      blurStrengthPercent: 0,
    },
    ranking: {
      topN: 5,
      windowMs: 2000,
      refreshIntervalMs: 250,
      excludedPrograms: [],
    },
    diagnostics: {
      logLevel: 'info',
    },
  },
  schema: {
    capsuleWidth: { min: 8, max: 24, unit: 'DIP', default: 10 },
    capsuleHeight: { min: 40, max: 160, unit: 'DIP', default: 64 },
    capsuleEndRounding: { min: 0, max: 12, unit: 'DIP', default: 5 },
    hoverDelay: { min: 0, max: 2000, step: 50, unit: 'ms', default: 350 },
    autoHideDelay: { min: 0, max: 5000, step: 100, unit: 'ms', default: 5000 },
    dismissDelay: { min: 0, max: 2000, step: 50, unit: 'ms', default: 500 },
    opacity: { min: 0, max: 100, step: 1, unit: '%', default: 92 },
    blurStrength: { min: 0, max: 100, step: 1, unit: '%', default: 60 },
    topN: { min: 1, max: 20, step: 1, unit: '', default: 5 },
    rankingWindow: { min: 500, max: 10000, step: 250, unit: 'ms', default: 2000 },
    refreshInterval: { min: 100, max: 5000, step: 50, unit: 'ms', default: 500 },
    speedUnits: ['auto', 'bytes', 'bits'],
    themes: ['system', 'light', 'dark'],
    languages: ['system', 'english', 'simplified_chinese'],
    logLevels: ['off', 'error', 'info', 'debug'],
    dockSides: ['left', 'right'],
  },
}

export class MockBridge extends NativeBridge {
  private timer: number | null = null
  private seq = 1
  private currentMockConfig: ConfigChangedDto = JSON.parse(JSON.stringify(defaultMockConfigChanged))
  private currentSurface: SurfaceChangedDto = { surface: 'capsule', pinned: false, dockSide: 'right' }
  private currentUpdateStatus: UpdateStatusDto = {
    state: 'Idle',
    currentVersion: '0.1.2',
    latestVersion: '',
    downloadProgressPercent: 0,
    errorMessage: '',
  }
  private currentConnectedApps: ConnectedApplicationsDto = {
    applications: [
      {
        id: 'executable:C:\\Program Files\\mihomo\\mihomo-multi.exe',
        executablePath: 'C:\\Program Files\\mihomo\\mihomo-multi.exe',
        executableName: 'mihomo-multi.exe',
        displayName: 'mihomo-multi.exe',
        iconKey: 'mihomo',
        totalDownloadBytes: 52428800,
        totalUploadBytes: 52428800,
      },
      {
        id: 'app-edge',
        executablePath: 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
        executableName: 'msedge.exe',
        displayName: 'Microsoft Edge',
        iconKey: 'msedge',
        totalDownloadBytes: 104857600,
        totalUploadBytes: 12582912,
      },
      {
        id: 'app-code',
        executablePath: 'C:\\Users\\User\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe',
        executableName: 'Code.exe',
        displayName: 'Visual Studio Code',
        iconKey: 'code',
        totalDownloadBytes: 25165824,
        totalUploadBytes: 8388608,
      },
      {
        id: 'app-steam',
        executablePath: 'C:\\Program Files (x86)\\Steam\\steam.exe',
        executableName: 'steam.exe',
        displayName: 'Steam Client Bootstrapper',
        iconKey: 'steam',
        totalDownloadBytes: 2147483648,
        totalUploadBytes: 41943040,
      },
    ],
  }
  constructor(autoTick = false) {
    super()
    if (autoTick && typeof window !== 'undefined') {
      this.startMockTicks()
    }
  }

  public override sendCommand<T = unknown>(type: WebCommandType, payload?: T, commandId?: string): void {
    super.sendCommand(type, payload, commandId)
    if (type === 'requestConfig') {
      setTimeout(() => {
        this.emit('configChanged', this.currentMockConfig)
        this.emit('updateStatus', this.currentUpdateStatus)
        this.emit('connectedApplications', this.currentConnectedApps)
      }, 0)
    } else if (type === 'showMonitor') {
      this.currentSurface = { ...this.currentSurface, surface: 'panel' }
      setTimeout(() => this.emit('surfaceChanged', this.currentSurface), 0)
    } else if (type === 'hideMonitor') {
      this.currentSurface = { ...this.currentSurface, surface: 'tray' }
      setTimeout(() => this.emit('surfaceChanged', this.currentSurface), 0)
    } else if (type === 'openSettings') {
      this.currentSurface = { ...this.currentSurface, surface: 'settings' }
      setTimeout(() => this.emit('surfaceChanged', this.currentSurface), 0)
    } else if (type === 'setPin' && payload) {
      const pinned = Boolean((payload as unknown as { pinned: boolean }).pinned)
      this.currentSurface = { ...this.currentSurface, surface: 'panel', pinned }
      setTimeout(() => this.emit('surfaceChanged', this.currentSurface), 0)
    } else if (type === 'updateSetting' && payload) {
      const p = payload as unknown as UpdateSettingPayload
      this.applyMockSettingUpdate(p.key, p.value)
      setTimeout(() => {
        this.emit('configChanged', this.currentMockConfig)
      }, 0)
    } else if (type === 'setTheme' && payload) {
      const t = (payload as unknown as { theme: 'System' | 'Light' | 'Dark' }).theme
      setTimeout(() => {
        this.emit('themeChanged', { theme: t })
      }, 0)
    } else if (type === 'setLocale' && payload) {
      const l = (payload as unknown as { locale: 'System' | 'English' | 'SimplifiedChinese' }).locale
      setTimeout(() => {
        this.emit('localeChanged', { locale: l })
      }, 0)
    } else if (type === 'retryCollector' || type === 'restartCollector') {
      setTimeout(() => {
        this.emit('collectorStatus', {
          monotonicMs: Date.now(),
          etwSessionActive: true,
          droppedPackets: 0,
          activePids: 20,
          state: 'Running',
          processId: 4321,
          instanceId: 'mock_restarted_inst',
          isElevated: true,
        })
      }, 50)
    } else if (type === 'checkForUpdate') {
      this.currentUpdateStatus = {
        ...this.currentUpdateStatus,
        state: 'Checking',
        errorMessage: '',
      }
      setTimeout(() => {
        this.emit('updateStatus', this.currentUpdateStatus)
      }, 0)
      setTimeout(() => {
        this.currentUpdateStatus = {
          ...this.currentUpdateStatus,
          state: 'UpToDate',
          latestVersion: this.currentUpdateStatus.currentVersion,
        }
        this.emit('updateStatus', this.currentUpdateStatus)
      }, 20)
    } else if (type === 'downloadAndApplyUpdate') {
      this.currentUpdateStatus = {
        ...this.currentUpdateStatus,
        state: 'Downloading',
        downloadProgressPercent: 45,
        errorMessage: '',
      }
      setTimeout(() => {
        this.emit('updateStatus', this.currentUpdateStatus)
      }, 0)
    } else if (type === 'requestConnectedApplications') {
      setTimeout(() => {
        this.emit('connectedApplications', this.currentConnectedApps)
      }, 0)
    }
  }

  private applyMockSettingUpdate(key: string, value: unknown): void {
    const cfg = this.currentMockConfig.config
    if (key === 'startup.ui_at_login' || key === 'general.uiAtLogin' || key === 'uiAtLogin') {
      cfg.general.uiAtLogin = Boolean(value)
    } else if (key === 'display.speed_unit' || key === 'general.speedUnit' || key === 'speedUnit') {
      cfg.general.speedUnit = value as 'auto' | 'bytes' | 'bits'
    } else if (key === 'appearance.theme' || key === 'theme') {
      cfg.appearance.theme = value as 'system' | 'light' | 'dark'
    } else if (key === 'appearance.language' || key === 'language') {
      cfg.appearance.language = value as 'system' | 'english' | 'simplified_chinese'
    } else if (key === 'floating.capsule_width_dip' || key === 'capsule.widthDip' || key === 'capsuleWidthDip') {
      cfg.capsule.widthDip = Number(value)
      cfg.capsule.effectiveEndRoundingDip = Math.min(cfg.capsule.endRoundingDip, Math.floor(cfg.capsule.widthDip / 2))
    } else if (key === 'floating.capsule_height_dip' || key === 'capsule.heightDip' || key === 'capsuleHeightDip') {
      cfg.capsule.heightDip = Number(value)
    } else if (
      key === 'floating.capsule_end_rounding_dip' ||
      key === 'capsule.endRoundingDip' ||
      key === 'capsuleEndRoundingDip'
    ) {
      cfg.capsule.endRoundingDip = Number(value)
      cfg.capsule.effectiveEndRoundingDip = Math.min(cfg.capsule.endRoundingDip, Math.floor(cfg.capsule.widthDip / 2))
    } else if (key === 'interaction.hover_delay_ms' || key === 'capsule.hoverDelayMs' || key === 'hoverDelayMs') {
      cfg.capsule.hoverDelayMs = Number(value)
    } else if (key === 'floating.anchor_side' || key === 'capsule.anchorSide' || key === 'anchorSide') {
      cfg.capsule.anchorSide = value as 'left' | 'right'
    } else if (key === 'floating.auto_hide_enabled' || key === 'capsule.autoHideEnabled' || key === 'autoHideEnabled') {
      cfg.capsule.autoHideEnabled = Boolean(value)
    } else if (
      key === 'floating.auto_hide_delay_ms' ||
      key === 'capsule.autoHideDelayMs' ||
      key === 'autoHideDelayMs'
    ) {
      cfg.capsule.autoHideDelayMs = 500
    } else if (
      key === 'floating.download_color_hex' ||
      key === 'capsule.downloadColorHex' ||
      key === 'downloadColorHex'
    ) {
      cfg.capsule.downloadColorHex = String(value)
    } else if (key === 'floating.upload_color_hex' || key === 'capsule.uploadColorHex' || key === 'uploadColorHex') {
      cfg.capsule.uploadColorHex = String(value)
    } else if (key === 'floating.nfm_pinned' || key === 'monitor.pinned' || key === 'pinned') {
      cfg.monitor.pinned = Boolean(value)
    } else if (key === 'interaction.dismiss_delay_ms' || key === 'monitor.dismissDelayMs' || key === 'dismissDelayMs') {
      cfg.monitor.dismissDelayMs = Number(value)
    } else if (key === 'floating.opacity_percent' || key === 'monitor.opacityPercent' || key === 'opacityPercent') {
      cfg.monitor.opacityPercent = Number(value)
    } else if (
      key === 'floating.blur_strength_percent' ||
      key === 'monitor.blurStrengthPercent' ||
      key === 'blurStrengthPercent'
    ) {
      cfg.monitor.blurStrengthPercent = Number(value)
    } else if (
      key === 'floating.show_capsule_border' ||
      key === 'capsule.showCapsuleBorder' ||
      key === 'showCapsuleBorder'
    ) {
      cfg.capsule.showCapsuleBorder = Boolean(value)
    } else if (
      key === 'floating.ignore_hover_when_fullscreen' ||
      key === 'capsule.ignoreHoverWhenFullscreen' ||
      key === 'ignoreHoverWhenFullscreen'
    ) {
      cfg.capsule.ignoreHoverWhenFullscreen = Boolean(value)
    } else if (
      key === 'floating.hide_capsule_when_fullscreen' ||
      key === 'capsule.hideCapsuleWhenFullscreen' ||
      key === 'hideCapsuleWhenFullscreen'
    ) {
      cfg.capsule.hideCapsuleWhenFullscreen = Boolean(value)
    } else if (key === 'ranking.top_n' || key === 'ranking.topN' || key === 'topN') {
      cfg.ranking.topN = Number(value)
    } else if (key === 'ranking.window_ms' || key === 'ranking.windowMs' || key === 'windowMs') {
      cfg.ranking.windowMs = Number(value)
    } else if (
      key === 'display.refresh_interval_ms' ||
      key === 'ranking.refreshIntervalMs' ||
      key === 'refreshIntervalMs'
    ) {
      cfg.ranking.refreshIntervalMs = Number(value)
    } else if (
      key === 'ranking.excluded_programs' ||
      key === 'ranking.excludedPrograms' ||
      key === 'excludedPrograms'
    ) {
      cfg.ranking.excludedPrograms = Array.isArray(value) ? value.map(v => String(v)) : []
    } else if (key === 'diagnostics.log_level' || key === 'diagnostics.logLevel' || key === 'logLevel') {
      cfg.diagnostics.logLevel = value as 'off' | 'error' | 'info' | 'debug'
    }
  }

  public startMockTicks(): void {
    if (this.timer) return
    this.timer = window.setInterval(() => {
      const now = Date.now()
      const mockTick: TrafficTickDto = {
        seq: this.seq++,
        monotonicMs: now,
        downloadBytesPerSec: Math.floor(1024 * 500 + Math.sin(now / 1000) * 1024 * 300),
        uploadBytesPerSec: Math.floor(1024 * 120 + Math.cos(now / 1000) * 1024 * 80),
      }
      this.emit('trafficTick', mockTick)

      if (this.seq % 4 === 0) {
        const mockStatus: CollectorStatusDto = {
          monotonicMs: now,
          etwSessionActive: true,
          droppedPackets: 0,
          activePids: 18,
          state: 'Running',
          processId: 2844,
          instanceId: 'mock_collector_session',
          isElevated: true,
        }
        this.emit('collectorStatus', mockStatus)

        const mockRanking: RankingSnapshotDto = {
          seq: Math.floor(this.seq / 4),
          monotonicMs: now,
          status: 'Ready',
          totalDownloadBytesPerSec: mockTick.downloadBytesPerSec,
          totalUploadBytesPerSec: mockTick.uploadBytesPerSec,
          applications: [
            {
              id: 'app-edge',
              executablePath: 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
              displayName: 'Microsoft Edge',
              iconKey: 'msedge',
              downloadBytesPerSec: Math.floor(mockTick.downloadBytesPerSec * 0.55),
              uploadBytesPerSec: Math.floor(mockTick.uploadBytesPerSec * 0.42),
            },
            {
              id: 'app-code',
              executablePath: 'C:\\Users\\User\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe',
              displayName: 'Visual Studio Code',
              iconKey: 'code',
              downloadBytesPerSec: Math.floor(mockTick.downloadBytesPerSec * 0.25),
              uploadBytesPerSec: Math.floor(mockTick.uploadBytesPerSec * 0.35),
            },
            {
              id: 'app-steam',
              executablePath: 'C:\\Program Files (x86)\\Steam\\steam.exe',
              displayName: 'Steam Client Bootstrapper',
              iconKey: 'steam',
              downloadBytesPerSec: Math.floor(mockTick.downloadBytesPerSec * 0.14),
              uploadBytesPerSec: Math.floor(mockTick.uploadBytesPerSec * 0.15),
            },
            {
              id: 'app-svchost',
              executablePath: 'C:\\Windows\\System32\\svchost.exe',
              displayName: 'Host Process for Windows Services',
              iconKey: 'svchost',
              downloadBytesPerSec: Math.floor(mockTick.downloadBytesPerSec * 0.06),
              uploadBytesPerSec: Math.floor(mockTick.uploadBytesPerSec * 0.08),
            },
          ],
        }
        this.emit('rankingSnapshot', mockRanking)
      }
    }, 250)
  }

  public stopMockTicks(): void {
    if (this.timer) {
      clearInterval(this.timer)
      this.timer = null
    }
  }

  public override destroy(): void {
    this.stopMockTicks()
    super.destroy()
  }

  public mockSessionReset(instanceId: string, pid = 1234): void {
    const reset: SessionResetDto = {
      collectorInstanceId: instanceId,
      collectorPid: pid,
    }
    this.emit('sessionReset', reset)
  }
}

export const defaultBridge: INativeBridge =
  typeof window !== 'undefined' && window.chrome?.webview?.postMessage ? new NativeBridge() : new MockBridge(true)
