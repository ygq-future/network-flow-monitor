export type LocaleMode = 'System' | 'English' | 'SimplifiedChinese'
export type ResolvedLocale = 'English' | 'SimplifiedChinese'

export interface TranslationSchema {
  common: {
    appName: string
    loading: string
    ready: string
    disconnected: string
    error: string
    download: string
    upload: string
    totalRate: string
    downloadRate: string
    uploadRate: string
    status: string
    activeProcesses: string
    droppedPackets: string
    settings: string
    pin: string
    unpin: string
    showMonitor: string
    hideMonitor: string
    retryCollector: string
    exitApp: string
  }
  tracer: {
    title: string
    subtitle: string
    liveThroughput: string
    liveThroughputDesc: string
    collectorHealth: string
    active: string
    inactive: string
    themeSwitch: string
    localeSwitch: string
    themeSystem: string
    themeLight: string
    themeDark: string
    localeSystem: string
    localeEnglish: string
    localeChinese: string
    settingsPlaceholder: string
    settingsPlaceholderDesc: string
    navToSettings: string
    backToTracer: string
  }
  settings: {
    title: string
    general: string
    appearance: string
    capsule: string
    monitor: string
    ranking: string
    collector: string
    diagnostics: string
    comingSoon: string
  }
}
