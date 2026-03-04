# Flask Mock Server - Setup & Testing Guide

## Overview
Complete mock robot backend with control endpoints, telemetry simulation, and MJPEG stream.

## Quick Start

### 1. Install Dependencies
```bash
pip3 install flask pillow numpy
```

### 2. Run the Server
```bash
cd /home/sd03/Documents/2026_Software_Projects/psvita-robot-console/tools
python3 flask_mock_server.py
```

Server will start on `http://0.0.0.0:5000`

The startup output will show:
```
🌐 Network Configuration
  Server IP (localhost):   http://127.0.0.1:5000
  Server IP (network):     http://192.168.1.100:5000  ← Use this for Vita3K
```

## Cross-Machine Setup (Vita3K on different computer)

If Vita3K emulator is on a different machine than the Flask server:

### Step 1: Get Flask Server IP
When the server starts, it shows your network IP (e.g., `192.168.1.100`).

### Step 2: Update robots.json
On the Vita3K machine, edit [../assets/robots.json](../assets/robots.json):
```json
{
  "robots": [
    "192.168.1.100",
    "192.168.1.100",
    "192.168.1.100"
  ]
}
```

### Step 3: Rebuild & Test
```bash
# On Vita3K machine
cd psvita-robot-console/build
cmake ..
make

# Then launch RobotConsole in Vita3K
```

### Automated Setup Helper
```bash
bash tools/setup_cross_machine.sh
```

See [CROSS_MACHINE_SETUP.md](CROSS_MACHINE_SETUP.md) for detailed instructions.

## API Endpoints

### POST /control
Send motion commands from the Vita app.

```bash
curl -X POST http://localhost:5000/control \
  -H "Content-Type: application/json" \
  -d '{
    "linear": 0.5,
    "angular": 0.2,
    "cam_pan": -0.1,
    "cam_tilt": 0.0,
    "lifter": 1,
    "estop": false
  }'
```

**Response:**
```json
{
  "status": "ok",
  "frame": 42,
  "timestamp": "2026-03-01T10:30:45.123456"
}
```

### GET /status
Retrieve robot telemetry.

```bash
curl http://localhost:5000/status
```

**Response:**
```json
{
  "battery": 83,
  "ping_ms": 18,
  "lifter_position": 50,
  "estop_active": false,
  "frame_count": 42,
  "uptime_seconds": 125
}
```

### GET /stream
Live MJPEG stream (30 fps, 640x480, 80% quality).

```bash
# Play with ffplay
ffplay http://localhost:5000/stream

# Or record to file
ffmpeg -i http://localhost:5000/stream -c copy stream.mpg
```

### GET /health
Simple health check.

```bash
curl http://localhost:5000/health
```

### GET /
Server information and current robot state.

```bash
curl http://localhost:5000/ | jq
```

## Simulated Features

✅ **Battery Drain** - Slowly decreases over time (configurable rate)
✅ **Command Timeout** - Stops motion if no command for 2+ seconds
✅ **E-Stop Override** - Immediately clears motion when activated
✅ **Lifter Simulation** - Incremental position changes
✅ **Network Jitter** - Ping varies 15-35ms (realistic WiFi)
✅ **MJPEG Stream** - Real-time status overlay on video frames
✅ **Thread-Safe** - All state updates are mutex-protected

## Testing Configuration

### For PS Vita App
Edit [robots.json](../assets/robots.json):
```json
{
  "robots": [
    "192.168.1.100",  // Replace with mock server IP
    "192.168.1.101",
    "192.168.1.102"
  ]
}
```

On same network as Vita:
```bash
# Find your machine's IP
ifconfig | grep "inet "

# Config file will connect to this IP:5000
```

If testing on same machine (localhost):
```json
{
  "robots": [
    "127.0.0.1",  // localhost
    "127.0.0.1",
    "127.0.0.1"
  ]
}
```

Then rebuild the app with new config.

### For Local Testing (curl/ffplay)
All endpoints accessible at `http://localhost:5000`

## Troubleshooting

**Port Already in Use:**
```bash
lsof -i :5000
kill -9 <pid>
```

**Flask Not Found:**
```bash
pip3 install --upgrade flask
```

**Pillow Issues:**
```bash
pip3 install --upgrade pillow
```

**Stream Not Playing:**
```bash
# Install ffmpeg/ffplay
sudo apt-get install ffmpeg

# Try with verbose output
ffplay -v debug http://localhost:5000/stream
```

## Performance Notes

- **Control latency**: ~5-10ms per POST request
- **Status latency**: ~2-5ms per GET request  
- **MJPEG FPS**: 30fps (configurable in code)
- **Frame quality**: 80% JPEG (configurable)
- **Memory usage**: ~50MB (typical)

## Modifying Behavior

Edit `flask_mock_server.py`:

```python
# Change battery drain rate (% per second)
battery_drain_rate = 0.05

# Change MJPEG frame rate
frame_interval = 1.0 / 60.0  # 60 FPS instead of 30

# Change JPEG quality
img.save(jpg_buffer, format='JPEG', quality=95)  # Higher quality
```

## Integration with Vita App

1. **Compile Vita app** with `apps/robots.json` pointing to server IP
2. **Run mock server**: `python3 flask_mock_server.py`
3. **Launch Vita app** (emulator or real hardware)
4. **Observe**:
   - Control commands POST to `/control`
   - Telemetry data fetched from `/status`
   - MJPEG stream decoded and displayed
   - Status indicators (green/yellow/red) responsive to ping

## Next Steps

- Test E-Stop: Press Circle button, verify `/status` shows `estop_active: true`
- Test Motion: Move left stick, verify `/control` POST messages
- Test Stream: Watch MJPEG display with status overlay
- Test Timeout: Hold controller still for 2+ seconds, motion should stop
- Test Battery: Watch battery % decrease in status bar

---

**Server Version:** 1.0
**Last Updated:** 2026-03-01
