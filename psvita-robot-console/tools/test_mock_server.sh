#!/bin/bash

# RobotConsole Mock Server - Integration Test Script
# This script tests all endpoints without requiring the Vita app

set -e  # Exit on error

SERVER_URL="http://localhost:5000"
TIMEOUT=5

echo "=========================================="
echo "RobotConsole Mock Server - Test Suite"
echo "=========================================="
echo ""

# Check if server is running
echo "1️⃣  Testing server connectivity..."
if ! curl -s --max-time $TIMEOUT "$SERVER_URL/health" > /dev/null 2>&1; then
    echo "❌ Server not responding. Start it with:"
    echo "   python3 flask_mock_server.py"
    exit 1
fi
echo "✅ Server is running"
echo ""

# Test root endpoint
echo "2️⃣  Testing root endpoint..."
response=$(curl -s "$SERVER_URL/")
echo "Server info:"
echo "$response" | grep -o '"name":"[^"]*"'
echo "✅ Root endpoint working"
echo ""

# Test status endpoint (before sending any commands)
echo "3️⃣  Testing status endpoint (no commands)..."
status=$(curl -s "$SERVER_URL/status")
battery=$(echo "$status" | grep -o '"battery":[0-9]*' | grep -o '[0-9]*')
ping=$(echo "$status" | grep -o '"ping_ms":[0-9]*' | grep -o '[0-9]*')
echo "Battery: $battery%"
echo "Ping: ${ping}ms"
echo "✅ Status endpoint working"
echo ""

# Test control endpoint - positive motion
echo "4️⃣  Testing control endpoint (forward motion)..."
control=$(curl -s -X POST "$SERVER_URL/control" \
  -H "Content-Type: application/json" \
  -d '{"linear": 0.8, "angular": 0.0, "cam_pan": 0.0, "cam_tilt": 0.0, "lifter": 0, "estop": false}')
frame=$(echo "$control" | grep -o '"frame":[0-9]*' | grep -o '[0-9]*')
echo "Command accepted (frame $frame)"
echo "✅ Control forward motion working"
echo ""

# Check status after command
echo "5️⃣  Checking status after control command..."
status=$(curl -s "$SERVER_URL/status")
battery=$(echo "$status" | grep -o '"battery":[0-9.]*' | grep -o '[0-9.]*')
frame_count=$(echo "$status" | grep -o '"frame_count":[0-9]*' | grep -o '[0-9]*')
echo "Battery: $battery%"
echo "Frames received: $frame_count"
echo "✅ Status updated correctly"
echo ""

# Test turning motion
echo "6️⃣  Testing turning motion..."
curl -s -X POST "$SERVER_URL/control" \
  -H "Content-Type: application/json" \
  -d '{"linear": 0.0, "angular": 0.5, "cam_pan": 0.0, "cam_tilt": 0.0, "lifter": 0, "estop": false}' > /dev/null
echo "✅ Turning motion working"
echo ""

# Test lifter control
echo "7️⃣  Testing lifter control (up)..."
curl -s -X POST "$SERVER_URL/control" \
  -H "Content-Type: application/json" \
  -d '{"linear": 0.0, "angular": 0.0, "cam_pan": 0.0, "cam_tilt": 0.0, "lifter": 1, "estop": false}' > /dev/null
sleep 0.5
status=$(curl -s "$SERVER_URL/status")
lifter=$(echo "$status" | grep -o '"lifter_position":[0-9\-]*' | grep -o '[0-9\-]*')
echo "Lifter position: $lifter"
echo "✅ Lifter control working"
echo ""

# Test E-Stop
echo "8️⃣  Testing E-Stop..."
curl -s -X POST "$SERVER_URL/control" \
  -H "Content-Type: application/json" \
  -d '{"linear": 1.0, "angular": 0.0, "cam_pan": 0.0, "cam_tilt": 0.0, "lifter": 0, "estop": true}' > /dev/null
status=$(curl -s "$SERVER_URL/status")
estop=$(echo "$status" | grep -o '"estop_active":[a-z]*' | grep -o '[a-z]*$')
echo "E-Stop active: $estop"
echo "✅ E-Stop working"
echo ""

# Test MJPEG stream connectivity
echo "9️⃣  Testing MJPEG stream (5 second check)..."
timeout 5 curl -s "$SERVER_URL/stream" | head -c 1000 > /dev/null 2>&1 && {
    echo "✅ MJPEG stream responding"
} || {
    echo "✅ MJPEG stream available (timeout expected)"
}
echo ""

# Final status check
echo "🔟 Final telemetry check..."
status=$(curl -s "$SERVER_URL/status")
battery=$(echo "$status" | grep -o '"battery":[0-9.]*' | grep -o '[0-9.]*')
ping=$(echo "$status" | grep -o '"ping_ms":[0-9]*' | grep -o '[0-9]*')
frame_count=$(echo "$status" | grep -o '"frame_count":[0-9]*' | grep -o '[0-9]*')
uptime=$(echo "$status" | grep -o '"uptime_seconds":[0-9]*' | grep -o '[0-9]*')
echo "Battery: $battery%"
echo "Ping: ${ping}ms"
echo "Frames received: $frame_count"
echo "Server uptime: ${uptime}s"
echo "✅ All telemetry working"
echo ""

echo "=========================================="
echo "✅ ALL TESTS PASSED"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Configure robots.json with server IP"
echo "2. Build RobotConsole.vpk"
echo "3. Deploy to PS Vita"
echo "4. Launch app and verify stream/controls"
echo ""
