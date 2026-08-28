import { describe, it, expect } from 'vitest'
import { formatRate, formatBytes } from '../shared/utils/formatRate'

describe('formatRate', () => {
  it('formats zero and negative rates correctly', () => {
    expect(formatRate(0)).toEqual({ value: '0.0', unit: 'KB/s', full: '0.0 KB/s' })
    expect(formatRate(-100)).toEqual({ value: '0.0', unit: 'KB/s', full: '0.0 KB/s' })
  })

  it('formats KB/s correctly', () => {
    expect(formatRate(1024)).toEqual({ value: '1.0', unit: 'KB/s', full: '1.0 KB/s' })
    expect(formatRate(512 * 1024)).toEqual({ value: '512.0', unit: 'KB/s', full: '512.0 KB/s' })
  })

  it('formats MB/s correctly', () => {
    expect(formatRate(1024 * 1024)).toEqual({ value: '1.0', unit: 'MB/s', full: '1.0 MB/s' })
    expect(formatRate(5.5 * 1024 * 1024)).toEqual({
      value: '5.5',
      unit: 'MB/s',
      full: '5.5 MB/s',
    })
  })

  it('formats GB/s correctly', () => {
    expect(formatRate(1024 * 1024 * 1024)).toEqual({
      value: '1.00',
      unit: 'GB/s',
      full: '1.00 GB/s',
    })
  })
})

describe('formatBytes', () => {
  it('formats byte totals accurately', () => {
    expect(formatBytes(0)).toBe('0 B')
    expect(formatBytes(1024)).toBe('1.0 KB')
    expect(formatBytes(1048576)).toBe('1.0 MB')
  })
})
