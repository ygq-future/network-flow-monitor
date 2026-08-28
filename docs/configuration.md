# Configuration

All public settings belong to one versioned native `AppConfig` model with defaults, units, bounds, migration and atomic persistence.

The Product Host is authoritative. React receives validated settings and submits only allow-listed `updateSetting` commands. A setting is effective after the host accepts it and echoes the authoritative state.

Public concepts include Top N, ranking window, display refresh interval, rate units, hover and dismiss delays, auto-hide enablement and delay, Pin, capsule width, capsule height, capsule end rounding, dock anchor, panel tint opacity, blur strength, theme, language, full-screen topmost yield, login startup and diagnostic log level.

Settings are organized as General, Appearance, Capsule, Monitor, Ranking, Collector and Diagnostics. Ticket 02 delivers this platform before capsule and monitor feature work so every later ticket can use authoritative live configuration during development and acceptance.

Configuration constants have one owner in the native `config` module. Each setting declares its default, unit, legal range and migration behavior beside its schema entry. Frontend controls consume the validated schema projection and do not duplicate defaults or ranges.

Time values use `std::chrono` in native interfaces. Corrupt configuration loads safe defaults while preserving a diagnosable damaged file. Unknown future schemas are not silently overwritten.
