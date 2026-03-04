#!/usr/bin/env python3
"""
Mock Flask Robot Server for RobotConsole Testing
Simulates a robot backend with control, status, and MJPEG stream endpoints
Run: python3 flask_mock_server.py
Then connect PS Vita app to http://localhost:5000
"""

import json
import io
import time
import threading
from datetime import datetime
from flask import Flask, request, Response, jsonify
from PIL import Image, ImageDraw
import numpy as np

app = Flask(__name__)

# Robot state
robot_state = {
    'linear_velocity': 0.0,
    'angular_velocity': 0.0,
    'cam_pan': 0.0,
    'cam_tilt': 0.0,
    'lifter_position': 0,  # -1=down, 0=idle, 1=up
    'estop_active': False,
    'battery_percent': 85,
    'ping_ms': 15,
    'uptime_seconds': 0,
    'frame_count': 0,
}

# Lock for thread-safe access
state_lock = threading.Lock()

# Simulated hardware
start_time = time.time()
battery_drain_rate = 0.02  # % per second
last_command_time = time.time()
command_timeout = 2.0  # seconds


def update_robot_state():
    """Update simulated robot state (battery drain, timeout, etc.)"""
    global last_command_time
    
    with state_lock:
        # Update battery (slow drain)
        robot_state['battery_percent'] = max(0, robot_state['battery_percent'] - battery_drain_rate * 0.01)
        
        # Command timeout - stop motion if no command for N seconds
        if time.time() - last_command_time > command_timeout:
            robot_state['linear_velocity'] = 0.0
            robot_state['angular_velocity'] = 0.0
        
        # E-Stop clears motion
        if robot_state['estop_active']:
            robot_state['linear_velocity'] = 0.0
            robot_state['angular_velocity'] = 0.0
        
        # Simulate lifter position changes
        if robot_state['lifter_position'] > 0:
            robot_state['lifter_position'] = min(100, robot_state['lifter_position'] + 1)
        elif robot_state['lifter_position'] < 0:
            robot_state['lifter_position'] = max(-100, robot_state['lifter_position'] - 1)
        
        # Update uptime
        robot_state['uptime_seconds'] = int(time.time() - start_time)
        
        # Simulate network jitter in ping (15-35ms)
        robot_state['ping_ms'] = 15 + int(np.random.normal(0, 5))
        robot_state['ping_ms'] = max(1, min(100, robot_state['ping_ms']))


def background_update_thread():
    """Continuously update robot state"""
    while True:
        update_robot_state()
        time.sleep(0.1)  # Update every 100ms


@app.route('/control', methods=['POST'])
def handle_control():
    """
    POST /control
    Accept motion control commands
    Expected JSON:
    {
        "linear": float (-1.0 to 1.0),
        "angular": float (-1.0 to 1.0),
        "cam_pan": float (-1.0 to 1.0),
        "cam_tilt": float (-1.0 to 1.0),
        "lifter": int (-1, 0, 1),
        "estop": bool
    }
    """
    global last_command_time
    
    try:
        data = request.get_json()
        
        with state_lock:
            # Update state from command
            robot_state['linear_velocity'] = float(data.get('linear', 0.0))
            robot_state['angular_velocity'] = float(data.get('angular', 0.0))
            robot_state['cam_pan'] = float(data.get('cam_pan', 0.0))
            robot_state['cam_tilt'] = float(data.get('cam_tilt', 0.0))
            robot_state['lifter_position'] = int(data.get('lifter', 0))
            robot_state['estop_active'] = bool(data.get('estop', False))
            
            last_command_time = time.time()
            robot_state['frame_count'] += 1
        
        return jsonify({
            'status': 'ok',
            'frame': robot_state['frame_count'],
            'timestamp': datetime.now().isoformat()
        }), 200
    
    except Exception as e:
        print(f"Error in /control: {e}")
        return jsonify({'error': str(e)}), 400


@app.route('/status', methods=['GET'])
def handle_status():
    """
    GET /status
    Return telemetry data
    Response JSON:
    {
        "battery": int (0-100),
        "ping_ms": int,
        "lifter_position": int,
        "estop_active": bool
    }
    """
    with state_lock:
        return jsonify({
            'battery': int(robot_state['battery_percent']),
            'ping_ms': robot_state['ping_ms'],
            'lifter_position': robot_state['lifter_position'],
            'estop_active': robot_state['estop_active'],
            'frame_count': robot_state['frame_count'],
            'uptime_seconds': robot_state['uptime_seconds']
        }), 200


def generate_test_frame(frame_num):
    """Generate a test JPEG frame with robot status overlay"""
    width, height = 640, 480
    
    # Create image with gradient background
    img = Image.new('RGB', (width, height), color='black')
    draw = ImageDraw.Draw(img)
    
    # Draw gradient (simple vertical lines)
    for y in range(height):
        color_val = int(30 + (y / height) * 100)
        draw.line([(0, y), (width, y)], fill=(color_val, color_val, color_val))
    
    # Add frame counter and status text
    text = f"Frame #{frame_num} - 640x480 Test Stream"
    draw.text((10, 10), text, fill='white')
    
    # Add status text
    with state_lock:
        status_lines = [
            f"Linear: {robot_state['linear_velocity']:.2f}",
            f"Angular: {robot_state['angular_velocity']:.2f}",
            f"Battery: {robot_state['battery_percent']:.1f}%",
            f"Ping: {robot_state['ping_ms']}ms",
            f"Lifter: {robot_state['lifter_position']:+d}",
            f"E-Stop: {'ACTIVE' if robot_state['estop_active'] else 'OK'}",
        ]
    
    y_offset = 50
    for line in status_lines:
        draw.text((10, y_offset), line, fill='white')
        y_offset += 30
    
    # Encode to JPEG
    jpg_buffer = io.BytesIO()
    img.save(jpg_buffer, format='JPEG', quality=80)
    jpg_buffer.seek(0)
    return jpg_buffer.getvalue()


@app.route('/stream', methods=['GET'])
def handle_stream():
    """
    GET /stream
    Return MJPEG stream
    Content-Type: multipart/x-mixed-replace; boundary=frame
    """
    def generate():
        frame_count = 0
        last_frame_time = time.time()
        frame_interval = 1.0 / 30.0  # 30 FPS
        
        while True:
            # Throttle frame rate
            elapsed = time.time() - last_frame_time
            if elapsed < frame_interval:
                time.sleep(frame_interval - elapsed)
            
            last_frame_time = time.time()
            frame_count += 1
            
            # Generate test frame
            frame_data = generate_test_frame(frame_count)
            
            # Send as multipart JPEG
            yield (
                b'--frame\r\n'
                b'Content-Type: image/jpeg\r\n'
                b'Content-Length: ' + str(len(frame_data)).encode() + b'\r\n'
                b'\r\n'
                + frame_data +
                b'\r\n'
            )
    
    return Response(
        generate(),
        mimetype='multipart/x-mixed-replace; boundary=frame'
    )


@app.route('/health', methods=['GET'])
def health_check():
    """Simple health check endpoint"""
    return jsonify({'status': 'healthy', 'timestamp': datetime.now().isoformat()}), 200


@app.route('/', methods=['GET'])
def index():
    """Root endpoint with server info"""
    with state_lock:
        state_copy = dict(robot_state)
    
    return jsonify({
        'name': 'RobotConsole Mock Server',
        'version': '1.0',
        'endpoints': {
            '/control': 'POST - Receive motion commands',
            '/status': 'GET - Return telemetry',
            '/stream': 'GET - MJPEG camera stream',
            '/health': 'GET - Health check',
        },
        'robot_state': state_copy
    }), 200


if __name__ == '__main__':
    import socket
    
    # Get local IP address
    def get_local_ip():
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return "127.0.0.1"
    
    local_ip = get_local_ip()
    
    print("=" * 70)
    print("  RobotConsole Mock Flask Server - MJPEG + Telemetry Simulator")
    print("=" * 70)
    print("\n🌐 Network Configuration")
    print(f"  Server IP (localhost):   http://127.0.0.1:5000")
    print(f"  Server IP (network):     http://{local_ip}:5000  ← Use this for Vita3K")
    print("\nℹ️  For Vita3K on another machine:")
    print(f"  Configure robots.json with: \"{local_ip}\"")
    print("\nAvailable Endpoints:")
    print("  📨  POST http://localhost:5000/control       - Send control commands")
    print("  📊  GET  http://localhost:5000/status        - Get telemetry")
    print("  📹  GET  http://localhost:5000/stream        - Get MJPEG stream")
    print("  🏥  GET  http://localhost:5000/health        - Health check")
    print("  ℹ️   GET  http://localhost:5000/             - Server info")
    print("\nUsage Examples:")
    print("  # Send control command:")
    print('  curl -X POST http://localhost:5000/control \\')
    print('    -H "Content-Type: application/json" \\')
    print('    -d \'{"linear": 0.5, "angular": 0.2, "lifter": 0, "estop": false}\'')
    print("\n  # Get telemetry:")
    print("  curl http://localhost:5000/status")
    print("\n  # View MJPEG stream:")
    print("  ffplay http://localhost:5000/stream")
    print("\n" + "=" * 70)
    print(f"Starting server on http://{local_ip}:5000 ...\n")
    
    # Start background update thread
    update_thread = threading.Thread(target=background_update_thread, daemon=True)
    update_thread.start()
    
    # Start Flask server
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
