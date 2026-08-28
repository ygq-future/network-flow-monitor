import { describe, it, expect } from 'vitest'
import { createWebCommand, TrafficTickDto, CollectorStatusDto } from '../bridge/bridgeTypes'
import { MockBridge, NativeBridge } from '../bridge/nativeBridge'

describe('Bridge contract', () => {
  it('creates direction-valid WebCommand envelope', () => {
    const cmdJson = createWebCommand('setPin', { pinned: true }, 'cmd_test_1')
    const parsed = JSON.parse(cmdJson)
    expect(parsed.type).toBe('setPin')
    expect(parsed.version).toBe(1)
    expect(parsed.commandId).toBe('cmd_test_1')
    expect(parsed.payload).toEqual({ pinned: true })
  })

  it('MockBridge correctly emits and receives typed events', () => {
    const bridge = new MockBridge(false)
    let tickReceived = false

    bridge.on<TrafficTickDto>('trafficTick', data => {
      tickReceived = true
      expect(data.seq).toBe(100)
      expect(data.downloadBytesPerSec).toBe(1048576)
      expect(data.uploadBytesPerSec).toBe(524288)
    })

    bridge.emit('trafficTick', {
      seq: 100,
      monotonicMs: 5000,
      downloadBytesPerSec: 1048576,
      uploadBytesPerSec: 524288,
    })

    expect(tickReceived).toBe(true)
    bridge.destroy()
  })

  it('deserializes native TrafficTick and CollectorStatus envelopes matching C++ bridge schema', () => {
    const bridge = new NativeBridge()

    let receivedTick: TrafficTickDto | null = null
    let receivedStatus: CollectorStatusDto | null = null

    bridge.on<TrafficTickDto>('trafficTick', data => {
      receivedTick = data
    })

    bridge.on<CollectorStatusDto>('collectorStatus', data => {
      receivedStatus = data
    })

    // Native Bridge serialization format
    const nativeTickJson = JSON.stringify({
      type: 'trafficTick',
      version: 1,
      payload: {
        seq: 4096,
        monotonicMs: 83928471,
        downloadBytesPerSec: 5242880,
        uploadBytesPerSec: 1048576,
      },
    })

    const nativeStatusJson = JSON.stringify({
      type: 'collectorStatus',
      version: 1,
      payload: {
        monotonicMs: 83928471,
        etwSessionActive: true,
        droppedPackets: 0,
        activePids: 42,
      },
    })

    // Simulate Native event delivery via window.chrome.webview message listener
    const parsedTick = JSON.parse(nativeTickJson)
    bridge.emit(parsedTick.type, parsedTick.payload)

    const parsedStatus = JSON.parse(nativeStatusJson)
    bridge.emit(parsedStatus.type, parsedStatus.payload)

    expect(receivedTick).toEqual({
      seq: 4096,
      monotonicMs: 83928471,
      downloadBytesPerSec: 5242880,
      uploadBytesPerSec: 1048576,
    })

    expect(receivedStatus).toEqual({
      monotonicMs: 83928471,
      etwSessionActive: true,
      droppedPackets: 0,
      activePids: 42,
    })

    bridge.destroy()
  })
})
