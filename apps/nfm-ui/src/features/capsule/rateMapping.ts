export interface RateTransitionConfig {
  durationMs: number
  easing: string
}

export const MAX_REFERENCE_RATE_BYTES_PER_SEC = 100 * 1024 * 1024 // 100 MB/s
export const MIN_VISIBLE_FILL_PERCENT = 3
export const BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC = 10 * 1024 // 10 KB/s
const RATE_CURVE_EXPONENT = 3
export const CAPSULE_ATTACK_ALPHA = 0.28
export const CAPSULE_RELEASE_ALPHA = 0.28
export const CAPSULE_IDLE_SNAP_BYTES_PER_SEC = 512

/**
 * Asymmetric exponential moving average (EMA) filter for 250 ms capsule ticks.
 * Dampens single-tick kernel/ETW burst spikes on attack while releasing gently
 * like a VU meter when traffic subsides.
 */
export function smoothCapsuleRate(previousSmoothedRate: number, rawSampleRate: number): number {
  const prev = Number.isFinite(previousSmoothedRate) && previousSmoothedRate > 0 ? previousSmoothedRate : 0
  const sample = Number.isFinite(rawSampleRate) && rawSampleRate > 0 ? rawSampleRate : 0

  const alpha = sample >= prev ? CAPSULE_ATTACK_ALPHA : CAPSULE_RELEASE_ALPHA
  const next = prev + alpha * (sample - prev)

  if (sample === 0 && next < CAPSULE_IDLE_SNAP_BYTES_PER_SEC) {
    return 0
  }
  return next
}
/**
 * Maps throughput rate (bytes/sec) to fill percentage [0, 100].
 * Rates up to 10 KB/s share the first MIN_VISIBLE_FILL_PERCENT linearly, so
 * background chatter stays near-idle. Above that the log-scaled position
 * between the floor and the ceiling is cubed, which keeps hundreds of KB/s
 * in the lower sixth (300 KB/s ≈ 14%) and places 1 MB/s at ~32% (roughly 1/3)
 * while 10 MB/s and above fill the range.
 * The ceiling adapts upward for bursts beyond 10 MB/s.
 */
export function calculateRateFillPercent(
  bytesPerSec: number,
  referenceCeilingBytesPerSec: number = MAX_REFERENCE_RATE_BYTES_PER_SEC,
): number {
  if (bytesPerSec <= 0 || !Number.isFinite(bytesPerSec)) {
    return 0
  }

  if (bytesPerSec <= BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC) {
    return (bytesPerSec / BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC) * MIN_VISIBLE_FILL_PERCENT
  }

  const ceiling = Math.max(2 * 1024 * 1024, referenceCeilingBytesPerSec)
  const logFloor = Math.log10(BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC)
  const logCeiling = Math.log10(ceiling)
  const logRate = Math.log10(bytesPerSec)
  const normalized = Math.min(1, Math.max(0, (logRate - logFloor) / (logCeiling - logFloor)))
  const curved = Math.pow(normalized, RATE_CURVE_EXPONENT)

  return MIN_VISIBLE_FILL_PERCENT + curved * (100 - MIN_VISIBLE_FILL_PERCENT)
}

/**
 * Returns transition timing based on whether rate is increasing or decreasing,
 * respecting prefers-reduced-motion.
 */
export function getRateTransition(
  currentVisualValue: number,
  targetValue: number,
  reducedMotion: boolean,
): RateTransitionConfig {
  if (reducedMotion) {
    return {
      durationMs: 0,
      easing: 'linear',
    }
  }

  if (targetValue >= currentVisualValue) {
    // Rate increasing: 300 ms cubic ease-out
    return {
      durationMs: 300,
      easing: 'cubic-bezier(0.33, 1, 0.68, 1)',
    }
  }

  // Rate decreasing: 650 ms gentle release ease-out
  return {
    durationMs: 650,
    easing: 'cubic-bezier(0.22, 1, 0.36, 1)',
  }
}
