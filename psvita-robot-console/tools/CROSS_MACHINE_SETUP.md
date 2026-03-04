# Cross-Machine Setup Guide for RobotConsole + Vita3K

## Scenario
- **Machine A**: Running Vita3K emulator with RobotConsole app
- **Machine B**: Running Flask mock server

Both machines must be on the same network (WiFi or Ethernet).

## Step 1: Find Flask Server IP Address

On Machine B (where Flask server will run):

```bash
# Linux/Mac
hostname -I
# or
ifconfig | grep "inet "

# Output should show something like: 192.168.1.100
```

Write down this IP address. Let's call it `<SERVER_IP>`.

## Step 2: Configure robots.json

On Machine A (Vita3K machine), edit [assets/robots.json](../assets/robots.json):

```json
{
  "robots": [
    "192.168.1.100",
    "192.168.1.100",
    "192.168.1.100"
  ]
}
```

Replace `192.168.1.100` with your Flask server's actual IP.

## Step 3: Rebuild RobotConsole.vpk

On Machine A:

```bash
cd psvita-robot-console/build
cmake ..
make
# New RobotConsole.vpk will be created with updated config
```

## Step 4: Start Flask Server

On Machine B:

```bash
cd psvita-robot-console/tools
python3 flask_mock_server.py
```

Output should show:
```
  RobotConsole Mock Flask Server - MJPEG + Telemetry Simulator
  Starting server on http://0.0.0.0:5000 ...
```

## Step 5: Load App in Vita3K

On Machine A:
1. Open Vita3K emulator
2. Install RobotConsole.vpk (just rebuilt)
3. Launch the app
4. The app will connect to Flask server at `<SERVER_IP>:5000`

## Verification

### Quick Test (from Vita3K machine)

```bash
# Test control endpoint
curl -X POST http://192.168.1.100:5000/control \
  -H "Content-Type: application/json" \
  -d '{"linear": 0.5, "angular": 0.0, "lifter": 0, "estop": false}'

# Check status
curl http://192.168.1.100:5000/status

# View stream
ffplay http://192.168.1.100:5000/stream
```

### Monitor Flask Server

Watch the Flask server terminal output:
- Shows POST requests from Vita3K control commands
- Shows GET requests for status
- Shows MJPEG stream connections
- Shows any connection errors

### App Status Indicators

In RobotConsole app:
- **Green dot** (top right) = Connected & good ping
- **Yellow dot** = Slow connection (ping > 150ms)
- **Red dot** = No connection (firewall or wrong IP)

## Common Issues

### 1. Red Connection Indicator

**Problem**: App shows red indicator instead of green

**Solutions**:
```bash
# Verify server IP is correct
ifconfig | grep inet

# Check firewall
sudo ufw allow 5000

# Verify both machines can ping each other
ping <SERVER_IP>

# Test manually
curl http://<SERVER_IP>:5000/health
```

### 2. "Connection refused"

**Problem**: Error message, can't reach Flask server

**Solutions**:
- Flask server not running (check Machine B terminal)
- Wrong IP in robots.json
- Firewall blocking port 5000
- Machines on different networks (use `ping`)

### 3. Slow/Dropping Packets

**Problem**: Yellow indicator, ping > 150ms

**Solutions**:
- WiFi interference (move closer to router)
- Network congestion (other downloads?)
- Check ping from terminal: `ping <SERVER_IP>`

### 4. MJPEG Stream Not Showing

**Problem**: App runs but camera area is black

**Solutions**:
- Network latency too high (check control latency requirement)
- MJPEG decoder issue (check app console)
- ffplay test: `ffplay http://<SERVER_IP>:5000/stream`

## Network Architecture

```
┌─────────────────────────────────────────────┐
│              Local Network                   │
│  (192.168.1.0/24 example)                   │
│                                              │
│  ┌──────────────────┐   Ethernet/WiFi       │
│  │  Machine A       │◄──────────────────────┤
│  │  Vita3K          │                        │
│  │  192.168.1.50    │                        │
│  └────────▲─────────┘                        │
│           │                                  │
│           │ HTTP:5000 POST /control          │
│           │ HTTP:5000 GET /status            │
│           │ HTTP:5000 GET /stream            │
│           │                                  │
│  ┌────────▼─────────┐                        │
│  │  Machine B       │                        │
│  │  Flask Server    │                        │
│  │  192.168.1.100   │                        │
│  └──────────────────┘                        │
│                                              │
└─────────────────────────────────────────────┘
```

## Automated Setup

Run the helper script:

```bash
bash tools/setup_cross_machine.sh
```

It will:
1. Detect your IP address
2. Help you configure the other machine
3. Provide next steps

## Firewall Configuration

### Linux (UFW)

```bash
# Allow Flask server port
sudo ufw allow 5000

# Verify
sudo ufw status
```

### Linux (iptables)

```bash
# Allow incoming on port 5000
sudo iptables -A INPUT -p tcp --dport 5000 -j ACCEPT
```

### macOS

```bash
# Firewall settings (System Preferences > Security & Privacy > Firewall Options)
# Or allow via terminal (Ventura+):
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --setallowsigned on
```

### Windows

1. Windows Defender Firewall > Allow an app through firewall
2. Add Python to allowed apps
3. Or disable firewall temporarily for testing

## Performance Notes

- **Typical latency**: 5-20ms on local network
- **Acceptable ping**: < 100ms (yellow at 100-150ms)
- **MJPEG FPS**: 30fps (configurable)
- **Control rate**: 30Hz (60fps app / 2)

---

**Next Steps**:
1. Run setup helper script
2. Update robots.json with server IP
3. Rebuild RobotConsole.vpk
4. Start Flask server
5. Launch in Vita3K
6. Verify green connection indicator
7. Test controls and stream

For issues, check Flask server console output for detailed error messages.
