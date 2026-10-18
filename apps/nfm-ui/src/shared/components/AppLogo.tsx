import React from 'react'

export interface AppLogoProps {
  className?: string
  size?: number
}

export const AppLogo: React.FC<AppLogoProps> = ({ className = 'h-5 w-5', size = 24 }) => {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 512 512"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      className={`shrink-0 select-none ${className}`}>
      <defs>
        <linearGradient id="nfm-logo-bg-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#27272A" />
          <stop offset="100%" stopColor="#09090B" />
        </linearGradient>
        <linearGradient id="nfm-logo-rim-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0.4" />
          <stop offset="100%" stopColor="#FFFFFF" stopOpacity="0.08" />
        </linearGradient>
        <linearGradient id="nfm-logo-down-grad" x1="184" y1="120" x2="184" y2="360" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#FFFFFF" />
          <stop offset="100%" stopColor="#D4D4D8" />
        </linearGradient>
        <linearGradient id="nfm-logo-up-grad" x1="328" y1="392" x2="328" y2="152" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#A1A1AA" />
          <stop offset="100%" stopColor="#71717A" />
        </linearGradient>
      </defs>

      <rect x="24" y="24" width="464" height="464" rx="116" fill="url(#nfm-logo-bg-grad)" />
      <rect
        x="24"
        y="24"
        width="464"
        height="464"
        rx="116"
        fill="none"
        stroke="url(#nfm-logo-rim-grad)"
        strokeWidth="6"
      />

      <g>
        <path d="M 188 136 L 188 336" stroke="url(#nfm-logo-down-grad)" strokeWidth="32" strokeLinecap="round" />
        <path
          d="M 132 284 L 188 344 L 244 284"
          fill="none"
          stroke="url(#nfm-logo-down-grad)"
          strokeWidth="32"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>

      <g>
        <path d="M 324 376 L 324 176" stroke="url(#nfm-logo-up-grad)" strokeWidth="32" strokeLinecap="round" />
        <path
          d="M 268 228 L 324 168 L 380 228"
          fill="none"
          stroke="url(#nfm-logo-up-grad)"
          strokeWidth="32"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>
    </svg>
  )
}
