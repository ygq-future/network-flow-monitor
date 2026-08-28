export interface FormattedRate {
  value: string
  unit: string
  full: string
}

export function formatRate(bytesPerSec: number | bigint): FormattedRate {
  const bytes = Number(bytesPerSec)
  if (!Number.isFinite(bytes) || bytes <= 0) {
    return { value: '0.0', unit: 'KB/s', full: '0.0 KB/s' }
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
