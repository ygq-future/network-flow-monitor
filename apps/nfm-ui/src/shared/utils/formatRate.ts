export interface FormattedRate {
  value: string
  unit: string
  full: string
}

function formatMagnitude(val: number): string {
  return val >= 100 ? Math.round(val).toString() : val.toFixed(1)
}

export function formatRate(bytesPerSec: number | bigint, unitMode: 'auto' | 'bytes' | 'bits' = 'auto'): FormattedRate {
  const bytes = Number(bytesPerSec)
  if (!Number.isFinite(bytes) || bytes <= 0) {
    if (unitMode === 'bits') {
      return { value: '0.0', unit: 'Kbps', full: '0.0 Kbps' }
    }
    return { value: '0.0', unit: 'KB/s', full: '0.0 KB/s' }
  }

  if (unitMode === 'bits') {
    const bits = bytes * 8
    const Kb = 1000
    const Mb = 1000 * Kb
    const Gb = 1000 * Mb
    if (bits >= 999.5 * Mb) {
      const value = formatMagnitude(bits / Gb)
      return { value, unit: 'Gbps', full: `${value} Gbps` }
    }
    if (bits >= 999.5 * Kb) {
      const value = formatMagnitude(bits / Mb)
      return { value, unit: 'Mbps', full: `${value} Mbps` }
    }
    const value = formatMagnitude(bits / Kb)
    return { value, unit: 'Kbps', full: `${value} Kbps` }
  }

  const KB = 1024
  const MB = 1024 * KB
  const GB = 1024 * MB

  let value: string
  let unit: string

  if (bytes >= 999.5 * MB) {
    value = formatMagnitude(bytes / GB)
    unit = 'GB/s'
  } else if (bytes >= 999.5 * KB) {
    value = formatMagnitude(bytes / MB)
    unit = 'MB/s'
  } else {
    value = formatMagnitude(bytes / KB)
    unit = 'KB/s'
  }

  return {
    value,
    unit,
    full: `${value} ${unit}`,
  }
}

export function formatBytes(totalBytes: number | bigint): string {
  const bytes = Number(totalBytes)
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return '0 B'
  }

  const KB = 1024
  const MB = 1024 * KB
  const GB = 1024 * MB
  const TB = 1024 * GB

  if (bytes >= 999.5 * GB) {
    return `${formatMagnitude(bytes / TB)} TB`
  }
  if (bytes >= 999.5 * MB) {
    return `${formatMagnitude(bytes / GB)} GB`
  }
  if (bytes >= 999.5 * KB) {
    return `${formatMagnitude(bytes / MB)} MB`
  }
  if (bytes >= 999.5) {
    return `${Math.max(1, Math.round(bytes / KB))} KB`
  }
  return `${Math.round(bytes)} B`
}
