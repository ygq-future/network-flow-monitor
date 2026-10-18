export interface RateTransitionConfig {
  durationMs: number
  easing: string
}

export const MAX_REFERENCE_RATE_BYTES_PER_SEC = 100 * 1024 * 1024 // 100 MB/s
export const MIN_VISIBLE_FILL_PERCENT = 3
export const BASE_SENSITIVITY_FLOOR_BYTES_PER_SEC = 512 // 0.5 KB/s

/**
 * Maps throughput rate (bytes/sec) smoothly to fill percentage [0, 100].
 * Uses a gentle log-power curve anchored at 512 B/s so low background rates
 * (10–50 KB/s) stay calm (~12–22%) while high-bandwidth bursts (10–100 MB/s)
 * expand across the full range. Supports an optional adaptive ceiling.
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
  const curved = Math.pow(normalized, 1.35)

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
    // Rate increasing: 250 ms cubic ease-out
    return {
      durationMs: 250,
      easing: 'cubic-bezier(0.33, 1, 0.68, 1)',
    }
  }

  // Rate decreasing: 450 ms quadratic ease-out
  return {
    durationMs: 450,
    easing: 'cubic-bezier(0.25, 1, 0.5, 1)',
  }
}
