# Quick Setup: Vita3K + Flask Server on Different Machines

## Your Setup
- **Machine A**: Vita3K emulator running
- **Machine B**: Flask mock server running

## 3-Step Setup

### Step 1: Start Flask Server (Machine B)
```bash
cd ~/psvita-robot-console/tools
python3 flask_mock_server.py
```

**Output will show:**
```
🌐 Network Configuration
  Server IP (localhost):   http://127.0.0.1:5000
  Server IP (network):     http://192.168.x.x:5000  ← COPY THIS IP
```

**Write down the network IP** (e.g., `192.168.1.100`)

### Step 2: Update robots.json (Machine A)
Edit `~/psvita-robot-console/assets/robots.json`:

```json
{
  "robots": [
    "192.168.1.100",
    "192.168.1.100",
    "192.168.1.100"
  ]
}
```
Replace `192.168.1.100` with the IP from Step 1.

### Step 3: Rebuild & Test (Machine A)
```bash
cd ~/psvita-robot-console/build
cmake ..
make
```

Then launch `RobotConsole.vpk` in Vita3K.

## Verify Connection

In Vita3K app, look at top-right corner:
- 🟢 **Green dot** = Connected ✅
- 🟡 **Yellow dot** = Slow (ping > 150ms)
- 🔴 **Red dot** = Not connected ❌

## If Red Dot (Not Connected)

### Check 1: Is Flask server running?
```bash
# On Machine B
curl http://localhost:5000/health
# Should show: {"status": "healthy", ...}
```

### Check 2: Can you reach the server?
```bash
# On Machine A
curl http://192.168.1.100:5000/health
# If this works, app should connect
```

### Check 3: Is robots.json correct?
```bash
# On Machine A
cat ~/psvita-robot-console/assets/robots.json
# Should show your Flask server IP
```

### Check 4: Same network?
```bash
# On Machine A
ping 192.168.1.100
# Should show responses (not "Unreachable")
```

### Check 5: Firewall blocking?
```bash
# On Machine B (if Linux)
sudo ufw allow 5000
# Then test on Machine A again
```

## Testing Endpoints

Once connected, test from Machine A:

```bash
# Test control
curl -X POST http://192.168.1.100:5000/control \
  -H "Content-Type: application/json" \
  -d '{"linear": 0.5, "angular": 0.0, "lifter": 0, "estop": false}'

# Test status
curl http://192.168.1.100:5000/status

# View MJPEG stream
ffplay http://192.168.1.100:5000/stream
```

## Network Troubleshooting Checklist

| Check | Command | Expected Result |
|-------|---------|-----------------|
| Server running? | `curl http://localhost:5000/health` (on Machine B) | `{"status": "healthy"}` |
| Network reachable? | `ping 192.168.1.100` (on Machine A) | `64 bytes from 192.168.x.x...` |
| Port open? | `curl http://192.168.1.100:5000/health` (on Machine A) | `{"status": "healthy"}` |
| Control works? | `curl -s http://192.168.1.100:5000/status` | `{"battery": XX, "ping_ms": YY, ...}` |
| Stream works? | `ffplay http://192.168.1.100:5000/stream` | Live video with status overlay |

## Common Errors

**"Connection refused"**
- Flask server not running on Machine B
- Wrong IP in robots.json
- Firewall blocking port 5000

**"No route to host"**
- Machines on different networks
- IP address typo
- Check with `ping 192.168.x.x`

**"Slow connection (yellow indicator)"**
- WiFi interference
- Network congestion
- Expected on some networks

**"Stream not showing (black screen)"**
- MJPEG decoder issue
- High latency (check with `ffplay`)
- Network drops

## Firewall Settings

### Linux (UFW)
```bash
# On Machine B
sudo ufw allow 5000
sudo ufw status
```

### macOS
```bash
# System Preferences > Security & Privacy > Firewall
# Or temporarily disable:
sudo defaults write /Library/Preferences/com.apple.alf globalstate -int 0
sudo launchctl stop com.apple.alf
```

### Windows
1. Open Windows Defender Firewall
2. Click "Allow an app through firewall"
3. Add Python or disable temporarily for testing

## Performance Notes

- **Typical latency**: 5-20ms same network
- **Green indicator**: < 100ms ping
- **Yellow indicator**: 100-150ms ping
- **Red indicator**: > 150ms or no connection
- **MJPEG FPS**: 30fps
- **Control rate**: 30Hz (sent every other frame)

## Help

For detailed setup guide, see: [CROSS_MACHINE_SETUP.md](CROSS_MACHINE_SETUP.md)

Automated setup helper:
```bash
bash tools/setup_cross_machine.sh
```

---

**Once you see the green indicator, the app is ready to control the robot!**

Try:
- Move left stick (forward/backward)
- Move right stick (turning)
- Press L/R triggers (lifter)
- Press Circle (E-Stop)
- Press Triangle (switch robot)
