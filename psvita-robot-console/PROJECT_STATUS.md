# RobotConsole - PS Vita Ground Robot Control System

**Author:** Ric
**Version:** 1.4.0
**Last Updated:** 2026-03-04
**Platform:** Jailbroken PS Vita (vitaSDK) + Vita3K emulator

---

## Core Features (Design Doc v1.0)

| Feature | Status | Notes |
|---|---|---|
| Control up to 6 robots sequentially over WiFi | DONE | Triangle cycles, D-pad L/R, touch robot name, add/remove in config |
| Live camera stream (MJPEG) | DONE | Snapshot polling with frame-skip optimization, ~300ms latency on real Vita |
| Drivetrain control (linear/angular) | DONE | Left analog stick |
| Camera pan/tilt | DONE | Right analog stick + rear touchpad |
| Lifter relay (up/down) | DONE | L/R triggers |
| E-Stop command | DONE | Circle button, latching toggle |
| Robot telemetry (battery, ping, connection) | DONE | Top status bar, JSON parsing fixed for Python spacing |
| Per-robot configuration | DONE | Start button opens config, IME editing, validation, saves to disk |
| HUD overlay on camera feed | DONE | Reticle, sliders, lift buttons, bars |
| App icon | DONE | 128x128 PNG in sce_sys/icon0.png |
| File logging (real hardware debug) | DONE | ux0:data/robot_console.log + ring buffer log viewer |

## Testing Plan Progress

| Stage | Status | Notes |
|---|---|---|
| Stage 1: Rectangle-only UI, verify rendering | DONE | Block font fallback + system PGF |
| Stage 2: Input movement bars | DONE | Analog sticks, deadzones, all buttons |
| Stage 3: Flask mock server, test control POST | DONE | tools/flask_mock_server_standalone.py |
| Stage 4: MJPEG stream | DONE | Snapshot polling verified on real PS Vita hardware |
| Stage 5: Full integration with VIAM | PENDING | Requires robot hardware |

## Definition of Done (v1.0) -- ALL MET

| Criterion | Status |
|---|---|
| Operator can switch between robots | DONE |
| Motion is smooth and responsive | DONE |
| Lifter responds instantly | DONE |
| E-Stop works reliably | DONE |
| Camera stream visible | DONE |
| Telemetry visible | DONE |
| No black screens | DONE |
| 60fps stable | DONE |

---

## v1.1 Features -- High Impact

| Feature | Status | Description |
|---|---|---|
| Startup/splash screen | DONE | Robot-themed splash with blue grid, tracked robot graphic, antenna + signal arcs |
| Camera screenshots (PNG) | DONE | Cross button saves PNG via libpng + sceDisplayGetFrameBuf, auto-incrementing filenames |
| Haptic feedback | N/A | PS Vita handheld has no vibration motor (sceCtrlSetActuator only works on paired DS3/DS4) |
| Dead man's switch | PLANNED | Server-side: auto-stop robot if no command received for 2 seconds |

## v1.1 Features -- Quality of Life

| Feature | Status | Description |
|---|---|---|
| Connection auto-retry + overlay | DONE | "RECONNECTING..." overlay with fail counter on connection loss |
| Audio feedback | DONE | 11 tones via SceAudio: safety (e-stop, alert, battery) + UI (screenshot, speed, switch, delete, save, error, ready) with mute toggle |
| Rear touchpad camera control | DONE | Back touchpad maps to camera pan/tilt as alternative input |
| Speed presets | DONE | D-Pad Up/Down cycles SLOW (0.3x) / MEDIUM (0.6x) / FAST (1.0x) |
| Connection timeout indicator | DONE | "LAST OK: Xs" in top bar when link fails |
| Camera latency display | DONE | Frame age in ms (color-coded green/amber/red) |
| Auto-dim HUD on idle | DONE | Dark overlay dims reticle/sliders after 5s of no input |
| Log viewer | DONE | Select button toggles overlay showing last 16 log entries (ring buffer) |
| Config screen ping test | DONE | Triangle button in config sends test connection, shows result for 3s |

## v1.1 Features -- Polish

| Feature | Status | Description |
|---|---|---|
| No-feed background | DONE | Crosshatch pattern + "NO SIGNAL" when camera unavailable |
| Version display on config screen | DONE | Shows version and build date in config screen header |
| Animated transitions | DEFERRED | Minimal benefit on Vita hardware, adds frame budget complexity |
| vita2d migration | DONE | Replaced SDL2+SDL2_ttf with vita2d+system PGF fonts for GPU-accelerated rendering |
| Speed preset badge | DONE | On-screen SLOW/MED/FAST indicator, color-coded (green/amber/red), always visible |
| FPS counter | DONE | Debug FPS display in bottom-right corner |
| Low battery warning | DONE | Red flash overlay when robot battery <= 15% |
| Screenshot flash feedback | DONE | Brief white flash + "SCREENSHOT SAVED" toast |
| Pulsing E-stop indicator | DONE | Sinusoidal breathing pulse animation on E-stop overlay |
| Line-segment circle rendering | DONE | Replaced Bresenham pixel circles with 64-segment line circles (fixes Vita3K artifacts) |

## v1.2 Features -- Sound Board

| Feature | Status | Description |
|---|---|---|
| Sound board screen | DONE | Square button opens dedicated sound board with 5 sound buttons |
| Sound playback via Flask | DONE | POST /sound endpoint triggers pygame MP3 playback on Raspberry Pi speaker |
| D-pad sound selection | DONE | Navigate 5 buttons (2 rows: 3+2) with visual highlight |
| Play feedback | DONE | "Playing: HORN..." toast with 2s fade on sound trigger |
| Canned messages / voice commands | DONE | Fulfilled by sound board — pre-recorded MP3s on robot (HORN, ALERT, BEEP, SIREN, CHIME) |

## v1.3 Features -- Reliability & Polish

| Feature | Status | Description |
|---|---|---|
| PNG screenshots | DONE | Replaced BMP with libpng for smaller files and better compatibility |
| Audio low-battery alerts | DONE | Descending chirp tone at 50%, 30%, and 15% battery thresholds (resets on robot swap) |
| Screenshot gallery viewer | DONE | Select cycles off -> log -> gallery -> off; D-pad navigates, loads PNGs via vita2d |
| LiveArea launcher page | DONE | Custom bg.png with baked-in version/controls/credits, startup gate image |
| Help / How-To screen | DONE | 3-page help accessible from Config via Select (Controls, Features, About/Credits) |
| PlayStation button colors | DONE | Canonical PS colors across all screens (Cross=blue, Circle=red, Triangle=green, Square=pink) |
| Telemetry color coding | DONE | Amber when sticks active, green when idle |
| Gallery delete | DONE | Triangle in gallery to delete screenshot (double-press confirm, 2s timeout) |

## v1.4 Features -- Operator Efficiency & Robustness

| Feature | Status | Description |
|---|---|---|
| Frame skip optimization | DONE | Skips RGB24->RGBA32 texture copy when camera frame hasn't changed (saves CPU on slow connections) |
| Network timeout tuning | DONE | Per-endpoint timeouts: control 80ms, status 150ms, sound 800ms, test 2500ms, MJPEG 1s+2s |
| Config field validation | DONE | IP format (x.x.x.x), port range (1-65535), endpoint auto-fix (prepends /), feedback messages |
| Startup timeout handling | DONE | 3-attempt connection test; user can press Start to skip to Config if robot unreachable |
| Add/remove robots in config | DONE | Circle adds (up to 6), Square removes (min 1), with validation feedback |
| Per-robot speed preset | DONE | Speed saved/loaded per robot on switch, persisted to robots.json |
| Config save confirmation | DONE | "CONFIG SAVED" green toast on control screen for 1.5s after exiting config |
| About/credits page | DONE | Help page 3: version, build date, credits (Ric, SirTechify), libraries, license |
| WiFi signal indicator | DONE | 4-bar RSSI indicator via sceNetCtlInetGetInfo, color-coded (red/amber/green) |
| UI sound effects | DONE | 6 new tones: screenshot click, speed pitch, robot switch chirp, delete, config save ding, error buzz |
| UI sounds mute toggle | DONE | Config field 5: toggle ON/OFF, mutes non-critical tones, safety tones always play, persisted in robots.json |

## v2.0 Features -- Advanced (Future)

| Feature | Status | Description |
|---|---|---|
| Map/minimap overlay | PLANNED | Trail map from robot odometry data |
| Macro recording | PLANNED | Record and replay control sequences |
| Multi-robot split view | PLANNED | Side-by-side camera feeds |
| Multi-robot simultaneous control | PLANNED | Design doc future hook |
| Autonomy toggle | PLANNED | Design doc future hook |
| Outdoor mode (reduced bandwidth) | PLANNED | Design doc future hook |

---

## Control Mapping (Current)

| Input | Action |
|---|---|
| Left Stick Y | Linear velocity (scaled by speed preset) |
| Left Stick X | Angular velocity (scaled by speed preset) |
| Right Stick X | Camera pan |
| Right Stick Y | Camera tilt |
| L Trigger | Lifter down |
| R Trigger | Lifter up |
| Triangle | Cycle to next robot (control) / Delete screenshot (gallery) |
| Circle | Toggle E-Stop (control, latching) / Add robot (config) |
| Cross | Save screenshot (control) / Play sound (sounds) / Edit field or toggle (config) |
| Square | Toggle sound board (control) / Remove robot (config) |
| Start | Open/close config screen / Return from help |
| Select | Cycle overlays: off -> log -> gallery -> off (control) / Open help (config) |
| D-Pad Up/Down | Cycle speed preset Slow/Med/Fast (control) / Navigate fields (config) |
| D-Pad Left/Right | Switch robot (control) / Navigate tabs (config) / Browse pages (help/gallery) |
| Front Touch (robot name) | Cycle to next robot |
| Rear Touchpad | Camera pan/tilt (alternative) |

---

## Flask Server API Contract (Integration Reference)

This section documents every HTTP endpoint the PS Vita client expects. Use this to build the real Flask server on the Raspberry Pi.

### Server Configuration

| Parameter | Value |
|---|---|
| Port | 5000 (TCP) |
| Protocol | HTTP (no TLS required) |
| Content-Type | application/json (all endpoints) |
| CORS | Not required (native client, not browser) |

### `POST /control` — Send Drive Commands

Called at **30 Hz** (every other frame of the 60 FPS main loop). This is the primary real-time control channel.

**Request Body:**
```json
{
    "linear": 0.45,
    "angular": -0.12,
    "cam_pan": 0.30,
    "cam_tilt": -0.15,
    "lifter": 1,
    "estop": false
}
```

| Field | Type | Range | Description |
|---|---|---|---|
| `linear` | float | -1.0 to 1.0 | Forward/backward velocity (scaled by speed preset on Vita side) |
| `angular` | float | -1.0 to 1.0 | Left/right angular velocity (scaled by speed preset on Vita side) |
| `cam_pan` | float | -1.0 to 1.0 | Camera horizontal pan position |
| `cam_tilt` | float | -1.0 to 1.0 | Camera vertical tilt position |
| `lifter` | int | -1, 0, 1 | Lifter direction: -1=down, 0=stop, 1=up |
| `estop` | bool | true/false | Emergency stop state (latching — Vita toggles, server should respect) |

**Expected Response:** HTTP 200 with any JSON body (response body is ignored by client).

**Timeout:** 80ms — if the server doesn't respond in 80ms, the client drops the request silently and sends the next one. Do not queue or buffer commands server-side.

**Dead Man's Switch (recommended):** If no `/control` POST arrives for 2+ seconds, the server should zero all velocities and stop the robot. The mock server already implements this.

### `GET /status` — Fetch Robot Telemetry

Called at **30 Hz** (same cadence as `/control`). Must be fast.

**Response Body:**
```json
{
    "battery": 85,
    "ping_ms": 15,
    "lifter_position": 0,
    "estop_active": false
}
```

| Field | Type | Range | Description |
|---|---|---|---|
| `battery` | int | 0-100 | Battery percentage. Triggers audio alerts on Vita at 50%, 30%, 15%. |
| `ping_ms` | int | >= 0 | Round-trip latency in ms. Values > 150 trigger NETWORK_STATUS_SLOW on Vita. |
| `lifter_position` | int | -100 to 100 | Current lifter position for display. |
| `estop_active` | bool | true/false | Server-side E-stop state. Shown on Vita HUD if active. |

**Timeout:** 150ms. Non-200 or timeout = NETWORK_STATUS_FAIL (red "RECONNECTING..." overlay).

**JSON Formatting:** The Vita parser uses `strstr` + `sscanf`, not a full JSON parser. It tolerates Python's `json.dumps()` spacing (e.g., `"battery": 85` with space after colon). Keys must be double-quoted. Boolean values must be lowercase `true`/`false`.

### `POST /sound` — Play Sound on Robot Speaker

Called on-demand when operator selects a sound from the sound board.

**Request Body:**
```json
{
    "sound_id": 0
}
```

| sound_id | Sound | Suggested File |
|---|---|---|
| 0 | HORN | horn.mp3 |
| 1 | ALERT | alert.mp3 |
| 2 | BEEP | beep.mp3 |
| 3 | SIREN | siren.mp3 |
| 4 | CHIME | chime.mp3 |

**Expected Response:** HTTP 200 with any JSON body.

**Timeout:** 800ms. Client shows "Playing: HORN..." toast regardless of response.

**Implementation:** On Raspberry Pi, use `pygame.mixer` to play MP3 files from a `/sounds/` directory. See mock server for example.

### `GET /webcam/?action=stream` — MJPEG Camera Stream

The Vita does **not** use this endpoint directly for streaming. Instead, it builds a **snapshot URL** by replacing `action=stream` with `action=snapshot` in the configured stream URL, then polls that snapshot endpoint repeatedly in a background thread.

**Snapshot URL (actually used):** `GET /webcam/?action=snapshot`

**Expected Response:** A single JPEG image (Content-Type: image/jpeg).

**Polling Rate:** As fast as possible (no delay between requests). Network latency + JPEG decode time naturally throttles to ~5-10 FPS.

**Timeouts:** 1s connect, 2s transfer. On error, retries after 500ms.

**Image Size:** Max 256KB JPEG. Images larger than 960x544 are automatically scaled down 2x by libjpeg. Recommended: 640x360 or 640x480.

**mjpg-streamer Compatibility:** The Vita is designed to work with [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer) running on the Pi. If you use mjpg-streamer, the stream URL `http://<ip>:<port>/webcam/?action=stream` is automatically converted to the snapshot URL `http://<ip>:<port>/webcam/?action=snapshot`. If your camera server doesn't follow this convention, set the `stream_endpoint` in the robot config to your snapshot URL directly.

### Camera Stream Configuration (Per-Robot)

Each robot in `robots.json` can have independent camera settings:

```json
{
    "robots": [
        {
            "ip": "192.168.1.100",
            "camera_host": "",
            "camera_port": 5000,
            "stream_endpoint": "/webcam/?action=stream",
            "speed_preset": 2
        }
    ],
    "ui_sounds_muted": 0
}
```

| Field | Default | Description |
|---|---|---|
| `ip` | required | Robot control server IP (for /control, /status, /sound) |
| `camera_host` | (same as ip) | Camera stream IP (if camera is on a different host) |
| `camera_port` | 5000 | Camera stream port (often 8080 for mjpg-streamer) |
| `stream_endpoint` | /stream | Stream URL path (converted to snapshot URL internally) |
| `speed_preset` | 2 (FAST) | Saved speed preset: 0=SLOW, 1=MED, 2=FAST |
| `ui_sounds_muted` | 0 | Global: 0=sounds on, 1=non-critical UI sounds muted |

**Stream URL Construction:** `http://{camera_host || ip}:{camera_port}{stream_endpoint}`

### Health/Discovery (Optional)

| Endpoint | Method | Description |
|---|---|---|
| `GET /health` | GET | Returns `{"status": "healthy"}`. Not used by Vita, useful for debugging. |
| `GET /` | GET | Returns server info + state. Not used by Vita, useful for debugging. |

---

## Network Timing Summary

| Operation | Frequency | Timeout | Notes |
|---|---|---|---|
| POST /control | 30 Hz | 80ms | Dropped silently on timeout |
| GET /status | 30 Hz | 150ms | Non-200 = FAIL state |
| POST /sound | On-demand | 800ms | Fire-and-forget feel |
| Connection test | On-demand | 2500ms | Used at startup and config Triangle button |
| MJPEG snapshot | Continuous (background thread) | 1s connect + 2s transfer | 500ms retry on error |

---

## Architecture

```
psvita-robot-console/
|-- source/
|   |-- main.c          Main loop, init, startup timeout, mode switching (Control/Config/Sounds/Gallery/Help)
|   |-- input.c/h       Analog sticks, buttons, front+rear touch, edge detection
|   |-- network.c/h     libcurl HTTP: control POST, status GET, sound POST, ping test, WiFi RSSI
|   |-- robot.c/h       Per-robot config (IP, camera, speed preset), add/remove, JSON save/load
|   |-- camera.c/h      Camera config (quality, fps, boundary, resolution)
|   |-- mjpeg.c/h       Snapshot polling JPEG decode (libjpeg), background thread, frame counter
|   |-- ui.c/h          vita2d HUD, config screen (validation, add/remove), sounds, gallery (delete),
|   |                    help (3 pages), overlays, PNG screenshots, config save toast, PS button colors
|   |-- feedback.c/h    Audio feedback (threaded tone generation via SceAudio, 11 tones, UI mute toggle)
|   |-- log.c/h         File logger + ring buffer (ux0:data/robot_console.log)
|   |-- version.h       App version (1.4.0), build date/time
|-- assets/
|   |-- icon0.png       App icon (128x128)
|   |-- robots.txt      Robot names list
|   |-- robots.json     Per-robot configuration (IP, camera, speed preset) + global settings (ui_sounds_muted)
|   |-- camera.json     Camera settings (boundary, resolution, quality)
|   |-- sce_sys/livearea/contents/
|       |-- template.xml    LiveArea layout (a1 style, bg + gate)
|       |-- bg.png          Background with baked-in version/controls/credits (840x500, 8-bit palette)
|       |-- startup.png     Gate image with robot graphic (280x158, 8-bit palette)
|-- tools/
|   |-- flask_mock_server.py
|   |-- flask_mock_server_standalone.py   Mock server with /control, /status, /sound, /webcam endpoints
|-- CMakeLists.txt
```

## Real Flask Server Implementation Checklist

When building the production server on the Raspberry Pi, implement these in order:

1. **`GET /status`** — Return real battery voltage (ADC), compute ping from request timing, read lifter encoder, track E-stop GPIO state
2. **`POST /control`** — Forward linear/angular to motor controller (VIAM SDK or direct PWM), cam_pan/tilt to servo controller, lifter to relay GPIO
3. **Dead man's switch** — Background thread zeros motors if no `/control` received for 2 seconds
4. **`POST /sound`** — `pygame.mixer.music.load/play` from `/home/pi/sounds/` directory
5. **Camera stream** — Run `mjpg-streamer` as a separate process on port 8080 (set `camera_port: 8080` in robots.json)
6. **E-stop hardware** — Wire physical E-stop button to GPIO, OR software E-stop from `/control` with hardware override

## Credits

- **Ric** — Developer
- **SirTechify** — Testing, feedback, and hardware validation
