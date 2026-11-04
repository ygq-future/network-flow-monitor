import { describe, it, expect } from 'vitest'
import {
  calculateRateFillPercent,
  getRateTransition,
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

  it('keeps background and hundreds-of-KB rates in the lower part of the range', () => {
    const tenMbCeiling = 10 * 1024 * 1024
    expect(calculateRateFillPercent(1024, tenMbCeiling)).toBeLessThan(MIN_VISIBLE_FILL_PERCENT)
    expect(calculateRateFillPercent(300 * 1024, tenMbCeiling)).toBeLessThan(30)
    expect(calculateRateFillPercent(1024 * 1024, tenMbCeiling)).toBeLessThan(50)
    expect(calculateRateFillPercent(tenMbCeiling, tenMbCeiling)).toBeCloseTo(100, 5)
  })

  it('provides asymmetric transitions for rising vs falling rates', () => {
    // Rate increasing: 250ms cubic ease-out
    const rising = getRateTransition(1000, 5000, false)
    expect(rising.durationMs).toBe(250)
    expect(rising.easing).toBe('cubic-bezier(0.33, 1, 0.68, 1)')

    // Rate decreasing: 450ms quadratic ease-out
    const falling = getRateTransition(5000, 1000, false)
    expect(falling.durationMs).toBe(450)
    expect(falling.easing).toBe('cubic-bezier(0.25, 1, 0.5, 1)')
  })

  it('respects prefers-reduced-motion with instant transitions (0ms)', () => {
    const risingReduced = getRateTransition(1000, 5000, true)
    expect(risingReduced.durationMs).toBe(0)

    const fallingReduced = getRateTransition(5000, 1000, true)
    expect(fallingReduced.durationMs).toBe(0)
  })
})
