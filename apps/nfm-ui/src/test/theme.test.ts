import { describe, it, expect } from 'vitest'
import { THEME_TOKENS } from '../shared/theme/themeTokens'

describe('themeTokens', () => {
  it('defines consistent semantic tokens for Light and Dark modes', () => {
    expect(THEME_TOKENS.Light.background).toBeDefined()
    expect(THEME_TOKENS.Dark.background).toBeDefined()
    expect(THEME_TOKENS.Light.download).toBeDefined()
    expect(THEME_TOKENS.Dark.download).toBeDefined()
    expect(THEME_TOKENS.Light.upload).toBeDefined()
    expect(THEME_TOKENS.Dark.upload).toBeDefined()
  })
})
