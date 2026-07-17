# Menu bar status icons

SF Symbols–inspired monochrome SVG icons for the HSRC OS menubar status cluster.

## Files

| File | Use |
|------|-----|
| `theme-sun.svg` | Shown in Dark mode (click → Light) |
| `theme-moon.svg` | Shown in Light mode (click → Dark) |
| `status-wifi.svg` | Wi‑Fi connected (bars clipped by signal level in UI) |
| `status-wifi-off.svg` | Wi‑Fi off / disconnected |
| `status-battery.svg` | Battery outline (fill level drawn in UI) |
| `status-bolt.svg` | Charging indicator overlay |

## Runtime

Icons are packed into the initrd (basename only) and loaded from
`/applications/<name>.svg` by `hsrc::sdk::SvgIcon` — a lightweight userspace
SVG subset renderer (viewBox, path M/L/H/V/C/Z, circle, rect, line, stroke/fill).

Tint color is applied at blit time so icons stay theme-aware on the acrylic bar.
