import React from 'react'
import { SettingsCenter, SettingsCenterProps } from './SettingsCenter'

export type SettingsPlaceholderProps = SettingsCenterProps

export const SettingsPlaceholder: React.FC<SettingsPlaceholderProps> = props => {
  return <SettingsCenter {...props} />
}
