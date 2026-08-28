import { describe, it, expect } from 'vitest'
import { enUS } from '../shared/i18n/en-US'
import { zhCN } from '../shared/i18n/zh-CN'

describe('i18n catalogs', () => {
  it('en-US and zh-CN have matching keys across all sections', () => {
    const checkKeysMatch = (objA: Record<string, unknown>, objB: Record<string, unknown>, path = '') => {
      const keysA = Object.keys(objA)
      const keysB = Object.keys(objB)
      expect(keysA.sort()).toEqual(keysB.sort())

      for (const k of keysA) {
        const valA = objA[k]
        const valB = objB[k]
        if (typeof valA === 'object' && valA !== null && typeof valB === 'object' && valB !== null) {
          checkKeysMatch(valA as Record<string, unknown>, valB as Record<string, unknown>, `${path}.${k}`)
        }
      }
    }

    checkKeysMatch(enUS as unknown as Record<string, unknown>, zhCN as unknown as Record<string, unknown>)
  })
})
