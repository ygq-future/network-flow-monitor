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
      className={`shrink-0 drop-shadow-xs select-none ${className}`}>
      <defs>
        {/* Background Gradient */}
        <linearGradient id="nfm-logo-bg-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#0F172A" />
          <stop offset="45%" stopColor="#1E1B4B" />
          <stop offset="100%" stopColor="#0B132B" />
        </linearGradient>

        {/* Outer Glow / Ambient Lighting */}
        <radialGradient id="nfm-logo-ambient-cyan" cx="160" cy="160" r="280" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#06B6D4" stopOpacity="0.35" />
          <stop offset="60%" stopColor="#06B6D4" stopOpacity="0.0" />
        </radialGradient>
        <radialGradient id="nfm-logo-ambient-purple" cx="352" cy="352" r="280" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#A855F7" stopOpacity="0.35" />
          <stop offset="60%" stopColor="#A855F7" stopOpacity="0.0" />
        </radialGradient>

        {/* Glass Rim Gradient */}
        <linearGradient id="nfm-logo-rim-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0.35" />
          <stop offset="35%" stopColor="#38BDF8" stopOpacity="0.2" />
          <stop offset="70%" stopColor="#C084FC" stopOpacity="0.2" />
          <stop offset="100%" stopColor="#FFFFFF" stopOpacity="0.1" />
        </linearGradient>

        {/* Download Stream Gradient (Cyan/Sky) */}
        <linearGradient id="nfm-logo-down-grad" x1="176" y1="110" x2="176" y2="380" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#38BDF8" />
          <stop offset="40%" stopColor="#00D2FF" />
          <stop offset="100%" stopColor="#0284C7" />
        </linearGradient>

        {/* Upload Stream Gradient (Neon Violet/Purple) */}
        <linearGradient id="nfm-logo-up-grad" x1="336" y1="402" x2="336" y2="132" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#C084FC" />
          <stop offset="40%" stopColor="#A855F7" />
          <stop offset="100%" stopColor="#7E22CE" />
        </linearGradient>

        {/* Central Telemetry Ring Gradient */}
        <linearGradient id="nfm-logo-ring-grad" x1="160" y1="160" x2="352" y2="352" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#38BDF8" stopOpacity="0.7" />
          <stop offset="50%" stopColor="#818CF8" stopOpacity="0.9" />
          <stop offset="100%" stopColor="#C084FC" stopOpacity="0.7" />
        </linearGradient>
      </defs>

      {/* Base Squircle Container */}
      <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#nfm-logo-bg-grad)" />
      <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#nfm-logo-ambient-cyan)" />
      <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#nfm-logo-ambient-purple)" />
      <rect
        x="24"
        y="24"
        width="464"
        height="464"
        rx="112"
        fill="none"
        stroke="url(#nfm-logo-rim-grad)"
        strokeWidth="4"
      />

      {/* Top Glass Highlight Reflection */}
      <path d="M 64 64 C 160 38, 352 38, 448 64 C 416 140, 96 140, 64 64 Z" fill="#FFFFFF" fillOpacity="0.04" />

      {/* Central Telemetry Orbit Ring & Pulse Wave */}
      <circle
        cx="256"
        cy="256"
        r="108"
        fill="none"
        stroke="url(#nfm-logo-ring-grad)"
        strokeWidth="5"
        strokeDasharray="14 10"
        opacity="0.65"
      />
      <circle
        cx="256"
        cy="256"
        r="148"
        fill="none"
        stroke="#6366F1"
        strokeWidth="2"
        strokeDasharray="6 14"
        opacity="0.3"
      />

      {/* Download Stream (Left - Inflow Downward) */}
      <g>
        <path d="M 184 120 L 184 320" stroke="#0369A1" strokeWidth="28" strokeLinecap="round" opacity="0.4" />
        <path d="M 184 120 L 184 316" stroke="url(#nfm-logo-down-grad)" strokeWidth="24" strokeLinecap="round" />
        <path
          d="M 124 264 L 184 328 L 244 264"
          fill="none"
          stroke="url(#nfm-logo-down-grad)"
          strokeWidth="24"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>

      {/* Upload Stream (Right - Outflow Upward) */}
      <g>
        <path d="M 328 392 L 328 192" stroke="#581C87" strokeWidth="28" strokeLinecap="round" opacity="0.4" />
        <path d="M 328 392 L 328 196" stroke="url(#nfm-logo-up-grad)" strokeWidth="24" strokeLinecap="round" />
        <path
          d="M 268 248 L 328 184 L 388 248"
          fill="none"
          stroke="url(#nfm-logo-up-grad)"
          strokeWidth="24"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </g>

      {/* Central Real-time Telemetry Core Orb */}
      <circle cx="256" cy="256" r="24" fill="#0F172A" stroke="url(#nfm-logo-ring-grad)" strokeWidth="4" />
      <circle cx="256" cy="256" r="14" fill="#38BDF8" opacity="0.9" />
      <circle cx="256" cy="256" r="6" fill="#FFFFFF" />

      {/* Dynamic Flow Particles */}
      <circle cx="184" cy="160" r="5" fill="#E0F2FE" />
      <circle cx="184" cy="220" r="4" fill="#BAE6FD" />
      <circle cx="328" cy="350" r="5" fill="#F3E8FF" />
      <circle cx="328" cy="290" r="4" fill="#E9D5FF" />
    </svg>
  )
}
