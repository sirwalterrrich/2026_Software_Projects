# PS Vita Robot Console

A PS Vita homebrew application to remotely control a fleet of up to 3 robots over a local network. Features a green-on-black tactical HUD with crosshair reticle, live telemetry, and per-robot camera stream configuration.

## Features

- **Gamepad & Touch Control**: Control robot movement using analog sticks, and camera via analog sticks or rear touchpad.
- **Tactical HUD**: Green-themed overlay with crosshair reticle, LIN/ANG sliders, lift indicators, and status bars. Features auto-retry and connection overlays.
- **Multi-Robot Support**: Configure up to 3 robots with individual IP, camera port, and stream endpoint.
- **In-App Configuration**: Tabbed config screen with PS Vita IME keyboard for editing fields. Shows app version.
- **Persistent Config**: Settings saved to disk as JSON, loaded on startup.
- **Live Camera Feed**: MJPEG stream display with per-robot URL derivation and "NO SIGNAL" background. Includes screenshot capability.
- **Lifter & Speed Control**: Raise/lower via L/R triggers and adjust movement speed across 3 presets via D-Pad.
- **Emergency Stop**: Circle button immediately halts all motion with red flash overlay, haptic rumble, and audio feedback.
- **Network Telemetry**: Real-time ping, battery, WiFi signal, and connection status.

## Screenshots

### Main Control Interface
![Main Control Interface](screenshots/main_interface.png)
*Main control screen showing tactical HUD with robot telemetry and camera feed*

### Configuration Screen
![Configuration Screen](screenshots/config_screen.png)
*Configuration interface for setting up robot IP addresses and camera endpoints*

### Emergency Stop
![Emergency Stop](screenshots/emergency_stop.png)
*Emergency stop activated with red overlay and warning indicators*

### Multi-Robot Selection
![Robot Selection](screenshots/robot_selection.png)
*Switching between multiple configured robots*

## Prerequisites

- [VitaSDK](https://vitasdk.org/) installed and configured (`VITASDK` environment variable set)
- CMake (version 3.10 or higher)

## Building

```bash
mkdir build
cd build
cmake ..
make
```

This generates `RobotConsole.vpk` for installation via VitaShell or Vita3K.

## Controls

### Control Mode

| Input | Action |
| --- | --- |
| **Left Stick** | Linear (Y) / Angular (X) velocity (scaled by speed preset) |
| **Right Stick** | Camera Pan (X) / Tilt (Y) |
| **L Trigger** | Lifter down (held) |
| **R Trigger** | Lifter up (held) |
| **Circle** | Toggle Emergency Stop (latching) |
| **Triangle** | Cycle to next robot |
| **Cross** | Save camera screenshot (ux0:data/RobotConsole/screenshots/) |
| **Start** | Open/close Config screen |
| **D-Pad Up/Down** | Cycle speed preset (Slow 0.3x / Med 0.6x / Fast 1.0x) |
| **Front Touch** | Tap robot name to cycle to next robot |
| **Rear Touchpad**| Camera pan/tilt (alternative) |

### Config Mode

| Input | Action |
| --- | --- |
| **D-Pad Left/Right** | Switch robot tab |
| **D-Pad Up/Down** | Navigate fields (IP / Port / Endpoint) |
| **Cross** | Edit selected field (opens IME keyboard) |
| **Start** | Save config and return to Control mode |

## Configuration

Robot settings are stored in `robots.json`:

```json
{
  "robots": [
    { "ip": "[IP_ADDRESS_1]", "camera_port": 5000, "stream_endpoint": "/stream" },
    { "ip": "[IP_ADDRESS_2]", "camera_port": 5000, "stream_endpoint": "/stream" },
    { "ip": "[IP_ADDRESS_3]", "camera_port": 5000, "stream_endpoint": "/stream" }
  ]
}
```

Each robot's camera stream URL is derived as: `http://{ip}:{camera_port}{stream_endpoint}`

The old flat string format (`"robots": ["[IP_ADDRESS]", ...]`) is still supported for backward compatibility.

Global camera settings (quality, fps, boundary, etc.) are in `camera.json`.

## Network Protocol

The console sends HTTP POST requests to the active robot at 30Hz:

**POST /control**
```json
{
  "linear": 0.00,
  "angular": 0.00,
  "cam_pan": 0.00,
  "cam_tilt": 0.00,
  "lifter": 0,
  "estop": 0
}
```

**GET /status** returns telemetry (battery, ping, estop state, etc.)

## Project Structure

- `source/main.c` - Application entry point, 60fps main loop, mode switching, splash screen
- `source/input.c` - PS Vita controller input with deadzone, touch, and button state tracking
- `source/network.c` - HTTP POST/GET communication via libcurl
- `source/robot.c` - Multi-robot config management with JSON load/save
- `source/camera.c` - Global camera stream settings
- `source/mjpeg.c` - MJPEG stream fetcher and frame parser
- `source/ui.c` - SDL2 HUD rendering, config screen, overlays, screenshots
- `source/log.c` - File logger (`ux0:data/robot_console.log`)
- `source/feedback.c` - Vita rumble and audio feedback

## Credits

**SirTechify** - Original concept and development inspiration
