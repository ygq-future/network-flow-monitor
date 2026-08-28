import React, { createContext, useContext, useEffect, useState, useMemo } from 'react'
import { INativeBridge, defaultBridge } from './nativeBridge'
import { TrafficTickDto, CollectorStatusDto, SessionResetDto } from './bridgeTypes'

interface BridgeContextValue {
  bridge: INativeBridge
  trafficTick: TrafficTickDto
  collectorStatus: CollectorStatusDto
  sessionReset: SessionResetDto | null
}

const defaultTick: TrafficTickDto = {
  seq: 0,
  monotonicMs: 0,
  downloadBytesPerSec: 0,
  uploadBytesPerSec: 0,
}

const defaultStatus: CollectorStatusDto = {
  monotonicMs: 0,
  etwSessionActive: false,
  droppedPackets: 0,
  activePids: 0,
}

const BridgeContext = createContext<BridgeContextValue | null>(null)

export interface BridgeProviderProps {
  children: React.ReactNode
  bridge?: INativeBridge
}

export const BridgeProvider: React.FC<BridgeProviderProps> = ({ children, bridge = defaultBridge }) => {
  const [trafficTick, setTrafficTick] = useState<TrafficTickDto>(defaultTick)
  const [collectorStatus, setCollectorStatus] = useState<CollectorStatusDto>(defaultStatus)
  const [sessionReset, setSessionReset] = useState<SessionResetDto | null>(null)

  useEffect(() => {
    const unsubTick = bridge.on<TrafficTickDto>('trafficTick', data => {
      setTrafficTick(data)
    })

    const unsubStatus = bridge.on<CollectorStatusDto>('collectorStatus', data => {
      setCollectorStatus(data)
    })

    const unsubReset = bridge.on<SessionResetDto>('sessionReset', data => {
      setSessionReset(data)
      // Reset rate on session reset
      setTrafficTick(defaultTick)
    })

    return () => {
      unsubTick()
      unsubStatus()
      unsubReset()
    }
  }, [bridge])

  const value = useMemo(
    () => ({
      bridge,
      trafficTick,
      collectorStatus,
      sessionReset,
    }),
    [bridge, trafficTick, collectorStatus, sessionReset],
  )

  return <BridgeContext.Provider value={value}>{children}</BridgeContext.Provider>
}

export function useBridge(): INativeBridge {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useBridge must be used within a BridgeProvider')
  }
  return context.bridge
}

export function useTrafficTick(): TrafficTickDto {
  const context = useContext(BridgeContext)
  if (!context) {
    throw new Error('useTrafficTick must be used within a BridgeProvider')
  }
  return context.trafficTick
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
