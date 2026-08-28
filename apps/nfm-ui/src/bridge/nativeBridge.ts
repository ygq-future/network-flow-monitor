import {
  NativeEventType,
  WebCommandType,
  NativeEventEnvelope,
  createWebCommand,
  TrafficTickDto,
  CollectorStatusDto,
  SessionResetDto,
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

export class MockBridge extends NativeBridge {
  private timer: number | null = null
  private seq = 1

  constructor(autoTick = false) {
    super()
    if (autoTick && typeof window !== 'undefined') {
      this.startMockTicks()
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
        }
        this.emit('collectorStatus', mockStatus)
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
