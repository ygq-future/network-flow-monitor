"""
Master App Icon Generator for Network Flow Monitor
Produces:
- resources/app_icon.ico (Windows multi-resolution PE icon: 16, 20, 24, 32, 40, 48, 64, 128, 256)
- apps/nfm-ui/public/favicon.svg
- apps/nfm-ui/public/favicon.ico
- apps/nfm-ui/public/app-icon-192.png
- apps/nfm-ui/public/app-icon-512.png
"""

import os
import io
import cairosvg
from PIL import Image

MASTER_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <defs>
    <!-- Background Gradient -->
    <linearGradient id="bg-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#0F172A" />
      <stop offset="45%" stop-color="#1E1B4B" />
      <stop offset="100%" stop-color="#0B132B" />
    </linearGradient>

    <!-- Outer Glow / Ambient Lighting -->
    <radialGradient id="ambient-cyan" cx="160" cy="160" r="280" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#06B6D4" stop-opacity="0.35" />
      <stop offset="60%" stop-color="#06B6D4" stop-opacity="0.0" />
    </radialGradient>
    <radialGradient id="ambient-purple" cx="352" cy="352" r="280" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#A855F7" stop-opacity="0.35" />
      <stop offset="60%" stop-color="#A855F7" stop-opacity="0.0" />
    </radialGradient>

    <!-- Glass Rim Gradient -->
    <linearGradient id="rim-grad" x1="64" y1="32" x2="448" y2="480" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#FFFFFF" stop-opacity="0.35" />
      <stop offset="35%" stop-color="#38BDF8" stop-opacity="0.2" />
      <stop offset="70%" stop-color="#C084FC" stop-opacity="0.2" />
      <stop offset="100%" stop-color="#FFFFFF" stop-opacity="0.1" />
    </linearGradient>

    <!-- Download Stream Gradient (Cyan/Sky) -->
    <linearGradient id="down-grad" x1="176" y1="110" x2="176" y2="380" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#38BDF8" />
      <stop offset="40%" stop-color="#00D2FF" />
      <stop offset="100%" stop-color="#0284C7" />
    </linearGradient>

    <!-- Upload Stream Gradient (Neon Violet/Purple) -->
    <linearGradient id="up-grad" x1="336" y1="402" x2="336" y2="132" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#C084FC" />
      <stop offset="40%" stop-color="#A855F7" />
      <stop offset="100%" stop-color="#7E22CE" />
    </linearGradient>

    <!-- Central Telemetry Ring Gradient -->
    <linearGradient id="ring-grad" x1="160" y1="160" x2="352" y2="352" gradientUnits="userSpaceOnUse">
      <stop offset="0%" stop-color="#38BDF8" stop-opacity="0.7" />
      <stop offset="50%" stop-color="#818CF8" stop-opacity="0.9" />
      <stop offset="100%" stop-color="#C084FC" stop-opacity="0.7" />
    </linearGradient>

    <!-- Stream Glow Filters -->
    <filter id="glow-cyan" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="8" result="blur" />
      <feComposite in="SourceGraphic" in2="blur" operator="over" />
    </filter>
    <filter id="glow-purple" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="8" result="blur" />
      <feComposite in="SourceGraphic" in2="blur" operator="over" />
    </filter>
  </defs>

  <!-- Base Squircle Container with Shadow -->
  <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#bg-grad)" />
  <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#ambient-cyan)" />
  <rect x="24" y="24" width="464" height="464" rx="112" fill="url(#ambient-purple)" />
  <rect x="24" y="24" width="464" height="464" rx="112" fill="none" stroke="url(#rim-grad)" stroke-width="4" />

  <!-- Top Glass Highlight Reflection -->
  <path d="M 64 64 C 160 38, 352 38, 448 64 C 416 140, 96 140, 64 64 Z" fill="#FFFFFF" fill-opacity="0.04" />

  <!-- Central Telemetry Orbit Ring & Pulse Wave -->
  <circle cx="256" cy="256" r="108" fill="none" stroke="url(#ring-grad)" stroke-width="5" stroke-dasharray="14 10" opacity="0.65" />
  <circle cx="256" cy="256" r="148" fill="none" stroke="#6366F1" stroke-width="2" stroke-dasharray="6 14" opacity="0.3" />

  <!-- Download Stream (Left - Inflow Downward) -->
  <g filter="url(#glow-cyan)">
    <!-- Stream Track/Backdrop -->
    <path d="M 184 120 L 184 320" stroke="#0369A1" stroke-width="28" stroke-linecap="round" opacity="0.4" />
    <!-- Active Speed Flow -->
    <path d="M 184 120 L 184 316" stroke="url(#down-grad)" stroke-width="24" stroke-linecap="round" />
    <!-- Arrow Head -->
    <path d="M 124 264 L 184 328 L 244 264" fill="none" stroke="url(#down-grad)" stroke-width="24" stroke-linecap="round" stroke-linejoin="round" />
  </g>

  <!-- Upload Stream (Right - Outflow Upward) -->
  <g filter="url(#glow-purple)">
    <!-- Stream Track/Backdrop -->
    <path d="M 328 392 L 328 192" stroke="#581C87" stroke-width="28" stroke-linecap="round" opacity="0.4" />
    <!-- Active Speed Flow -->
    <path d="M 328 392 L 328 196" stroke="url(#up-grad)" stroke-width="24" stroke-linecap="round" />
    <!-- Arrow Head -->
    <path d="M 268 248 L 328 184 L 388 248" fill="none" stroke="url(#up-grad)" stroke-width="24" stroke-linecap="round" stroke-linejoin="round" />
  </g>

  <!-- Central Real-time Telemetry Core Orb -->
  <circle cx="256" cy="256" r="24" fill="#0F172A" stroke="url(#ring-grad)" stroke-width="4" />
  <circle cx="256" cy="256" r="14" fill="#38BDF8" opacity="0.9" />
  <circle cx="256" cy="256" r="6" fill="#FFFFFF" />

  <!-- Dynamic Flow Particles / Speed Accents -->
  <circle cx="184" cy="160" r="5" fill="#E0F2FE" />
  <circle cx="184" cy="220" r="4" fill="#BAE6FD" />
  <circle cx="328" cy="350" r="5" fill="#F3E8FF" />
  <circle cx="328" cy="290" r="4" fill="#E9D5FF" />
</svg>"""

def generate():
    os.makedirs("resources", exist_ok=True)
    os.makedirs("apps/nfm-ui/public", exist_ok=True)

    # 1. Save canonical SVG
    with open("apps/nfm-ui/public/favicon.svg", "w", encoding="utf-8") as f:
        f.write(MASTER_SVG)
    print("Wrote apps/nfm-ui/public/favicon.svg")

    # 2. Render multi-resolution PNGs
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256, 512]
    pil_images = {}
    for s in sizes:
        png_data = cairosvg.svg2png(bytestring=MASTER_SVG.encode("utf-8"), output_width=s, output_height=s)
        img = Image.open(io.BytesIO(png_data)).convert("RGBA")
        pil_images[s] = img

    # Save web high-res PNGs
    pil_images[192 if 192 in pil_images else 256].save("apps/nfm-ui/public/app-icon-192.png", "PNG")
    # Generate 192 directly
    png_192 = cairosvg.svg2png(bytestring=MASTER_SVG.encode("utf-8"), output_width=192, output_height=192)
    with open("apps/nfm-ui/public/app-icon-192.png", "wb") as f:
        f.write(png_192)
    pil_images[512].save("apps/nfm-ui/public/app-icon-512.png", "PNG")

    # 3. Package Windows multi-res .ico
    # Windows standard ICO sizes: 256, 128, 64, 48, 40, 32, 24, 20, 16
    ico_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (40, 40), (32, 32), (24, 24), (20, 20), (16, 16)]
    ico_imgs = [pil_images[w] for w, h in ico_sizes]

    # Save to resources/app_icon.ico
    ico_imgs[0].save(
        "resources/app_icon.ico",
        format="ICO",
        sizes=ico_sizes,
        append_images=ico_imgs[1:]
    )
    print("Wrote resources/app_icon.ico with sizes:", [f"{w}x{h}" for w, h in ico_sizes])

    # Save to apps/nfm-ui/public/favicon.ico
    ico_imgs[0].save(
        "apps/nfm-ui/public/favicon.ico",
        format="ICO",
        sizes=[(64, 64), (32, 32), (16, 16)],
        append_images=[pil_images[32], pil_images[16]]
    )
    print("Wrote apps/nfm-ui/public/favicon.ico")

if __name__ == "__main__":
    generate()
