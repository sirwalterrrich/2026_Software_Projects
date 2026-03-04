# RobotConsole Technology Roadmap

**Author:** Ric
**Last Updated:** 2026-03-03

---

## v1.4 — Operator Efficiency & Robustness (Current)

| Feature | Priority | Description |
|---|---|---|
| Frame skip on slow connection | DONE | Skips texture copy when camera frame unchanged, saves CPU |
| Network timeout tuning | DONE | Per-endpoint timeouts (80ms control, 150ms status, 800ms sound) |
| Config field validation | DONE | IP format, port range, endpoint auto-fix with feedback messages |
| Startup timeout handling | DONE | 3-attempt test, Start skips to config if robot unreachable |
| Add/remove robots in config | DONE | Circle adds (max 6), Square removes (min 1) |
| Per-robot speed preset | DONE | Speed saved/loaded per robot, persisted to robots.json |
| Config save confirmation | DONE | "CONFIG SAVED" toast on return to control screen |
| About/credits page | DONE | Help page 3 with version, build date, credits, libraries |
| WiFi signal indicator | DONE | 4-bar RSSI from sceNetCtlInetGetInfo, color-coded |
| Gallery delete | DONE | Triangle double-press to delete screenshot with confirmation |
| Help screen | DONE | 3 pages: Controls, Features, About/Credits |
| PlayStation button colors | DONE | Canonical PS colors across all screens |
| UI sound effects | DONE | 6 new tones: screenshot, speed, switch, delete, save, error |
| UI sounds mute toggle | DONE | Config toggle, mutes non-critical tones, safety tones unaffected |
| Dead man's switch | HIGH | Server-side auto-stop if no command received for 2 seconds |
| Graceful disconnect on robot switch | MEDIUM | Zero-out commands to previous robot before connecting to new one |

## v2.0 — Situational Awareness

| Feature | Priority | Description |
|---|---|---|
| Map/minimap overlay | HIGH | Breadcrumb trail plot from robot odometry data |
| Heading/compass overlay | MEDIUM | Show robot orientation on HUD (requires IMU on Pi) |
| Camera stream recording | MEDIUM | Save MJPEG stream to file for post-mission review |
| Telemetry CSV logging | MEDIUM | Log battery, ping, commands to CSV on Vita for post-session analysis |
| Adjustable camera quality | LOW | Toggle resolution/compression from config screen to adapt to WiFi |

## v2.1 — Operator Efficiency

| Feature | Priority | Description |
|---|---|---|
| Macro recording/playback | HIGH | Record and replay control sequences for repetitive tasks |
| Pinch-to-zoom camera | MEDIUM | Two-finger front touchscreen zoom on camera feed |
| Night/high-contrast mode | LOW | Boost or invert camera feed for low-light environments (pixel LUT) |

## v3.0 — Multi-Robot & Autonomy

| Feature | Priority | Description |
|---|---|---|
| Split-view cameras | HIGH | Side-by-side camera feeds from two robots |
| Simultaneous control | HIGH | Broadcast commands to multiple robots at once |
| Waypoint navigation | MEDIUM | Tap-to-drive on minimap (requires autonomous nav on robot) |
| Object detection overlay | LOW | Lightweight inference on Pi, bounding box overlay on stream |

---

## Version History

| Version | Date | Highlights |
|---|---|---|
| v1.0 | 2026-02 | Core control, camera, telemetry, config, HUD |
| v1.1 | 2026-03 | Splash screen, screenshots, audio, speed presets, auto-dim, log viewer |
| v1.2 | 2026-03 | Sound board (5 sounds via Flask/pygame), PNG screenshots |
| v1.3 | 2026-03 | Battery alerts, gallery viewer, LiveArea, help screen, PS button colors |
| v1.4 | 2026-03 | Startup timeout, add/remove robots, per-robot speed, WiFi RSSI, config validation, frame skip |
