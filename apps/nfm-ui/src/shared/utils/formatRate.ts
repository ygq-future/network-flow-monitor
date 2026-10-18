export interface FormattedRate {
  value: string
  unit: string
  full: string
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
    if (bits >= Gb) {
      const value = (bits / Gb).toFixed(2)
      return { value, unit: 'Gbps', full: `${value} Gbps` }
    }
    if (bits >= Mb) {
      const value = (bits / Mb).toFixed(1)
      return { value, unit: 'Mbps', full: `${value} Mbps` }
    }
    const value = (bits / Kb).toFixed(1)
    return { value, unit: 'Kbps', full: `${value} Kbps` }
  }
  const KB = 1024
  const MB = 1024 * KB
  const GB = 1024 * MB

  let value: string
  let unit: string

  if (bytes >= GB) {
    value = (bytes / GB).toFixed(2)
    unit = 'GB/s'
  } else if (bytes >= MB) {
    value = (bytes / MB).toFixed(1)
    unit = 'MB/s'
  } else if (bytes >= KB) {
    value = (bytes / KB).toFixed(1)
    unit = 'KB/s'
  } else {
    value = (bytes / KB).toFixed(1)
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

  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let i = 0
  let val = bytes
  while (val >= 1024 && i < units.length - 1) {
    val /= 1024
    i++
  }

  return `${val.toFixed(i === 0 ? 0 : 1)} ${units[i]}`
}
