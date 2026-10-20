import React, { createContext, useContext, useEffect, useState, useMemo } from 'react'
import { INativeBridge, defaultBridge, defaultMockConfigChanged } from './nativeBridge'
import {
  TrafficTickDto,
  RankingSnapshotDto,
  CollectorStatusDto,
  SessionResetDto,
  SurfaceChangedDto,
  AppConfigDto,
  ConfigChangedDto,
} from './bridgeTypes'

export interface BridgeContextValue {
  bridge: INativeBridge
  trafficTick: TrafficTickDto
  rankingSnapshot: RankingSnapshotDto
  collectorStatus: CollectorStatusDto
  sessionReset: SessionResetDto | null
  surface: SurfaceChangedDto
  config: AppConfigDto
}

const defaultTick: TrafficTickDto = {
  seq: 0,
  monotonicMs: 0,
  downloadBytesPerSec: 0,
  uploadBytesPerSec: 0,
}

const defaultRanking: RankingSnapshotDto = {
  seq: 0,
  monotonicMs: 0,
  status: 'Ready',
  totalDownloadBytesPerSec: 0,
  totalUploadBytesPerSec: 0,
  applications: [],
}

const defaultStatus: CollectorStatusDto = {
  monotonicMs: 0,
  etwSessionActive: false,
  droppedPackets: 0,
  activePids: 0,
}

const defaultSurface: SurfaceChangedDto = {
  surface: 'capsule',
  pinned: false,
  dockSide: 'right',
}
export const BridgeContext = createContext<BridgeContextValue | null>(null)
const TrafficTickContext = createContext<TrafficTickDto | null>(null)
const RankingSnapshotContext = createContext<RankingSnapshotDto | null>(null)
export interface BridgeProviderProps {
  children: React.ReactNode
  bridge?: INativeBridge
}

export const BridgeProvider: React.FC<BridgeProviderProps> = ({ children, bridge = defaultBridge }) => {
  const [trafficTick, setTrafficTick] = useState<TrafficTickDto>(defaultTick)
  const [rankingSnapshot, setRankingSnapshot] = useState<RankingSnapshotDto>(defaultRanking)
  const [collectorStatus, setCollectorStatus] = useState<CollectorStatusDto>(defaultStatus)
  const [sessionReset, setSessionReset] = useState<SessionResetDto | null>(null)
  const [surface, setSurface] = useState<SurfaceChangedDto>(defaultSurface)
  const [config, setConfig] = useState<AppConfigDto>(defaultMockConfigChanged.config)
  useEffect(() => {
    const unsubTick = bridge.on<TrafficTickDto>('trafficTick', data => {
      setTrafficTick(prev =>
        prev.downloadBytesPerSec === data.downloadBytesPerSec && prev.uploadBytesPerSec === data.uploadBytesPerSec
          ? prev
          : data,
      )
    })

    const unsubRanking = bridge.on<RankingSnapshotDto>('rankingSnapshot', data => {
      setRankingSnapshot(data)
    })

    const unsubStatus = bridge.on<CollectorStatusDto>('collectorStatus', data => {
      setCollectorStatus(data)
      const isConnected = data.etwSessionActive && data.state !== 'Stopped' && data.state !== 'Failed'
      if (!isConnected) {
        setTrafficTick(defaultTick)
        setRankingSnapshot(defaultRanking)
      }
    })

    const unsubReset = bridge.on<SessionResetDto>('sessionReset', data => {
      setSessionReset(data)
      // Reset rate on session reset
      setTrafficTick(defaultTick)
      setRankingSnapshot(defaultRanking)
    })
    const unsubSurface = bridge.on<SurfaceChangedDto>('surfaceChanged', setSurface)
    const unsubConfig = bridge.on<ConfigChangedDto>('configChanged', data => {
      setConfig(data.config)
    })
    bridge.sendCommand('requestConfig')

    return () => {
      unsubTick()
      unsubRanking()
      unsubStatus()
      unsubReset()
      unsubSurface()
      unsubConfig()
    }
  }, [bridge])

  const value = useMemo(
    () => ({
      bridge,
      trafficTick: defaultTick,
      rankingSnapshot: defaultRanking,
      collectorStatus,
      sessionReset,
      surface,
      config,
    }),
    [bridge, collectorStatus, sessionReset, surface, config],
  )
  return (
    <BridgeContext.Provider value={value}>
      <TrafficTickContext.Provider value={trafficTick}>
        <RankingSnapshotContext.Provider value={rankingSnapshot}>{children}</RankingSnapshotContext.Provider>
      </TrafficTickContext.Provider>
    </BridgeContext.Provider>
  )
}

export function useBridge(): INativeBridge {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useBridge must be used within a BridgeProvider')
  }
  return context.bridge
}

export function useTrafficTick(): TrafficTickDto {
  const tick = useContext(TrafficTickContext)
  if (!tick) {
    throw new Error('useTrafficTick must be used within a BridgeProvider')
  }
  return tick
}

export function useRankingSnapshot(): RankingSnapshotDto {
  const ranking = useContext(RankingSnapshotContext)
  if (!ranking) {
    throw new Error('useRankingSnapshot must be used within a BridgeProvider')
  }
  return ranking
}

export function useCollectorStatus(): CollectorStatusDto {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useCollectorStatus must be used within a BridgeProvider')
  }
  return context.collectorStatus
}

export function useSessionReset(): SessionResetDto | null {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useSessionReset must be used within a BridgeProvider')
  }
  return context.sessionReset
}

export function useSurface(): SurfaceChangedDto {
  const context = useContext(BridgeContext)
  if (!context) throw new Error('useSurface must be used within a BridgeProvider')
  return context.surface
}

export function useAppConfig(): AppConfigDto {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useAppConfig must be used within a BridgeProvider')
  }
  return context.config
}
