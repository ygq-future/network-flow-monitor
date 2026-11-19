import { describe, it, expect } from 'vitest'
import {
  calculateRateFillPercent,
  getRateTransition,
  smoothCapsuleRate,
  MAX_REFERENCE_RATE_BYTES_PER_SEC,
  BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC,
  MIN_VISIBLE_FILL_PERCENT,
} from '../features/capsule/rateMapping'

describe('rateMapping', () => {
  it('maps zero or invalid rates to 0%', () => {
    expect(calculateRateFillPercent(0)).toBe(0)
    expect(calculateRateFillPercent(-100)).toBe(0)
    expect(calculateRateFillPercent(NaN)).toBe(0)
    expect(calculateRateFillPercent(Infinity)).toBe(0)
  })

  it('maps positive rates logarithmically', () => {
    const lowRate = calculateRateFillPercent(BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC)
    const midRate = calculateRateFillPercent(1024 * 1024) // 1 MB/s
    const maxRate = calculateRateFillPercent(MAX_REFERENCE_RATE_BYTES_PER_SEC) // 100 MB/s

    expect(lowRate).toBeCloseTo(MIN_VISIBLE_FILL_PERCENT, 5)
    expect(midRate).toBeGreaterThan(lowRate)
    expect(maxRate).toBeCloseTo(100, 1)
  })

  it('keeps hundreds-of-KB rates low and places 1 MB/s at roughly one-third of a 10 MB/s ceiling', () => {
    const tenMbCeiling = 10 * 1024 * 1024
    expect(calculateRateFillPercent(1024, tenMbCeiling)).toBeLessThan(MIN_VISIBLE_FILL_PERCENT)
    expect(calculateRateFillPercent(300 * 1024, tenMbCeiling)).toBeLessThan(20)
    const oneMbPercent = calculateRateFillPercent(1024 * 1024, tenMbCeiling)
    expect(oneMbPercent).toBeGreaterThan(30)
    expect(oneMbPercent).toBeLessThan(35)
    expect(calculateRateFillPercent(tenMbCeiling, tenMbCeiling)).toBeCloseTo(100, 5)
  })

  it('keeps rising and falling transitions within the 250 ms tick cadence', () => {
    const rising = getRateTransition(1000, 5000, false)
    expect(rising.durationMs).toBe(200)
    expect(rising.easing).toBe('cubic-bezier(0.33, 1, 0.68, 1)')

    const falling = getRateTransition(5000, 1000, false)
    expect(falling.durationMs).toBe(250)
    expect(falling.easing).toBe('cubic-bezier(0.22, 1, 0.36, 1)')
  })

  it('tracks rising traffic promptly and decays briskly without lingering', () => {
    const burstRate = 2 * 1024 * 1024
    const afterAttack = smoothCapsuleRate(0, burstRate)
    expect(afterAttack).toBeGreaterThanOrEqual(burstRate * 0.75)
    expect(afterAttack).toBeLessThan(burstRate)

    const afterFirstDecay = smoothCapsuleRate(afterAttack, 0)
    expect(afterFirstDecay).toBeGreaterThan(0)
    expect(afterFirstDecay).toBeLessThan(afterAttack * 0.5)

    // Decays to 0 within a few ticks once below sub-pixel idle threshold, while preserving steady low traffic
    expect(smoothCapsuleRate(10_000, 0)).toBe(0)
    expect(smoothCapsuleRate(0, 2048)).toBeGreaterThan(0)
  })

  it('respects prefers-reduced-motion with instant transitions (0ms)', () => {
    const risingReduced = getRateTransition(1000, 5000, true)
    expect(risingReduced.durationMs).toBe(0)

    const fallingReduced = getRateTransition(5000, 1000, true)
    expect(fallingReduced.durationMs).toBe(0)
  })
})
