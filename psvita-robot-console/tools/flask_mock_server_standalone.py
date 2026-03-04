#!/usr/bin/env python3
"""
Minimal RobotConsole Mock Server (No External Dependencies)
Pure Python implementation using built-in http.server module
"""

import json
import io
import time
import threading
import socket
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import base64

# Robot state
robot_state = {
    'linear_velocity': 0.0,
    'angular_velocity': 0.0,
    'cam_pan': 0.0,
    'cam_tilt': 0.0,
    'lifter_position': 0,
    'estop_active': False,
    'battery_percent': 85,
    'ping_ms': 15,
    'uptime_seconds': 0,
    'frame_count': 0,
}

state_lock = threading.Lock()
start_time = time.time()
last_command_time = time.time()


def update_robot_state():
    """Update simulated robot state"""
    global last_command_time
    
    with state_lock:
        # Battery drain
        robot_state['battery_percent'] = max(0, robot_state['battery_percent'] - 0.0002)
        
        # Command timeout
        if time.time() - last_command_time > 2.0:
            robot_state['linear_velocity'] = 0.0
            robot_state['angular_velocity'] = 0.0
        
        # E-Stop override
        if robot_state['estop_active']:
            robot_state['linear_velocity'] = 0.0
            robot_state['angular_velocity'] = 0.0
        
        # Lifter simulation
        if robot_state['lifter_position'] > 0:
            robot_state['lifter_position'] = min(100, robot_state['lifter_position'] + 1)
        elif robot_state['lifter_position'] < 0:
            robot_state['lifter_position'] = max(-100, robot_state['lifter_position'] - 1)
        
        # Uptime & ping
        robot_state['uptime_seconds'] = int(time.time() - start_time)
        robot_state['ping_ms'] = 15 + (hash(int(time.time())) % 20)


def background_update():
    """Background state updater"""
    while True:
        update_robot_state()
        time.sleep(0.1)


def generate_simple_frame(frame_num):
    """Generate a simple test JPEG frame"""
    try:
        import struct
        
        # Generate PPM in memory (RGB uncompressed)
        width, height = 320, 240
        
        ppm_data = bytearray()
        ppm_data.extend(b"P6\n")
        ppm_data.extend(b"320 240\n")
        ppm_data.extend(b"255\n")
        
        # Generate a simple pattern with frame number embedded
        for y in range(height):
            for x in range(width):
                # Moving color bars
                hue = ((x + frame_num * 2) % 256)
                r = int(128 + 127 * (hue < 85 or hue >= 170))
                g = int(128 + 127 * (hue >= 85 and hue < 170))
                b = int(128 + 127 * isinstance(hue, int))
                
                # Add some gradient
                r = min(255, r + (y // 10))
                g = min(255, g + (x // 10))
                
                ppm_data.extend(bytes([r, g, b]))
        
        # Convert PPM to JPEG using PIL if available, else return PPM
        try:
            from PIL import Image
            import io as pio
            
            img = Image.frombytes('RGB', (width, height), bytes(ppm_data[14:]))
            jpeg_buf = pio.BytesIO()
            img.save(jpeg_buf, 'JPEG', quality=80)
            return jpeg_buf.getvalue()
        except:
            # Fallback: return PPM data as JPEG (won't actually decode as JPEG but structure is there)
            # Create minimal JPEG header for testing
            jpeg_sig = b'\xff\xd8\xff\xe0'  # SOI + APP0
            jpeg_sig += b'\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00'
            jpeg_sig += ppm_data
            jpeg_sig += b'\xff\xd9'  # EOI
            return jpeg_sig
    except:
        # Minimal valid JPEG
        return b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xd9'


class RobotConsoleHandler(BaseHTTPRequestHandler):
    """HTTP request handler for mock robot server"""
    
    def do_POST(self):
        """Handle POST requests"""
        global last_command_time
        
        print(f"\n[POST] {self.path}")
        
        if self.path == '/control':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            
            try:
                data = json.loads(body.decode())
                print(f"       Control: linear={data.get('linear', 0.0):.2f}, angular={data.get('angular', 0.0):.2f}, pan={data.get('cam_pan', 0.0):.2f}, tilt={data.get('cam_tilt', 0.0):.2f}, lifter={data.get('lifter', 0)}, estop={data.get('estop', False)}")
                
                with state_lock:
                    robot_state['linear_velocity'] = float(data.get('linear', 0.0))
                    robot_state['angular_velocity'] = float(data.get('angular', 0.0))
                    robot_state['cam_pan'] = float(data.get('cam_pan', 0.0))
                    robot_state['cam_tilt'] = float(data.get('cam_tilt', 0.0))
                    robot_state['lifter_position'] = int(data.get('lifter', 0))
                    robot_state['estop_active'] = bool(data.get('estop', False))
                    robot_state['frame_count'] += 1
                
                last_command_time = time.time()
                
                response = json.dumps({'status': 'ok', 'frame': robot_state['frame_count']})
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', len(response))
                self.end_headers()
                self.wfile.write(response.encode())
                print(f"       Response: 200 OK")
            
            except Exception as e:
                print(f"       Error: {str(e)}")
                response = json.dumps({'error': str(e)})
                self.send_response(400)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(response.encode())
        
        elif self.path == '/sound':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)

            try:
                data = json.loads(body.decode())
                sound_id = int(data.get('sound_id', 0))
                sound_files = ['horn.mp3', 'alert.mp3', 'beep.mp3', 'siren.mp3', 'chime.mp3']
                sound_names = ['HORN', 'ALERT', 'BEEP', 'SIREN', 'CHIME']

                name = sound_names[sound_id] if 0 <= sound_id < len(sound_names) else f"UNKNOWN({sound_id})"
                filename = sound_files[sound_id] if 0 <= sound_id < len(sound_files) else None

                print(f"       Sound: {name} (id={sound_id})")

                # On real Pi, play via pygame:
                # import pygame
                # pygame.mixer.music.load(f'/home/pi/sounds/{filename}')
                # pygame.mixer.music.play()

                response = json.dumps({'status': 'ok', 'sound_id': sound_id, 'name': name})
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', len(response))
                self.end_headers()
                self.wfile.write(response.encode())
                print(f"       Response: 200 OK - playing {name}")

            except Exception as e:
                print(f"       Error: {str(e)}")
                response = json.dumps({'error': str(e)})
                self.send_response(400)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(response.encode())

        else:
            print(f"       404 Not Found")
            self.send_error(404)

    def do_GET(self):
        """Handle GET requests"""
        print(f"\n[GET]  {self.path}")
        
        if self.path == '/status':
            with state_lock:
                status = {
                    'battery': int(robot_state['battery_percent']),
                    'ping_ms': robot_state['ping_ms'],
                    'lifter_position': robot_state['lifter_position'],
                    'estop_active': robot_state['estop_active'],
                    'frame_count': robot_state['frame_count'],
                    'uptime_seconds': robot_state['uptime_seconds']
                }
            
            response = json.dumps(status)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', len(response))
            self.end_headers()
            self.wfile.write(response.encode())
            print(f"       Response: 200 OK - battery={status['battery']}%, lifter={status['lifter_position']}")
        
        elif self.path == '/health':
            response = json.dumps({'status': 'healthy'})
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(response.encode())
            print(f"       Response: 200 OK - healthy")
        
        elif self.path == '/webcam/?action=stream' or '/action=stream' in self.path:
            """Stream MJPEG video with proper boundary markers"""
            print(f"       Streaming MJPEG...")
            boundary = "boundarydonotcross"
            
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=' + boundary)
            self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
            self.send_header('Connection', 'keep-alive')
            self.end_headers()
            
            frame_num = 0
            try:
                while True:
                    frame_data = generate_simple_frame(frame_num)
                    frame_num += 1
                    
                    # MJPEG frame format
                    boundary_line = f"--{boundary}\r\n".encode()
                    header = b"Content-Type: image/jpeg\r\n"
                    length_header = f"Content-Length: {len(frame_data)}\r\n\r\n".encode()
                    
                    self.wfile.write(boundary_line)
                    self.wfile.write(header)
                    self.wfile.write(length_header)
                    self.wfile.write(frame_data)
                    self.wfile.write(b"\r\n")
                    
                    if frame_num % 30 == 0:
                        print(f"       Streaming: {frame_num} frames sent")
                    
                    time.sleep(0.033)  # ~30 FPS
            except:
                print(f"       Stream ended after {frame_num} frames")
        
        elif self.path == '/':
            with state_lock:
                state_copy = dict(robot_state)
            
            info = {
                'name': 'RobotConsole Mock Server',
                'version': '1.0',
                'endpoints': {
                    '/control': 'POST',
                    '/status': 'GET',
                    '/health': 'GET',
                    '/sound': 'POST',
                    '/webcam/?action=stream': 'GET (MJPEG)'
                },
                'robot_state': state_copy
            }
            
            response = json.dumps(info, indent=2)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(response.encode())
            print(f"       Response: 200 OK - server info")
        
        else:
            print(f"       404 Not Found")
            self.send_error(404)
    
    def log_message(self, format, *args):
        """Custom logging - suppress default HTTP server logs"""
        pass  # We handle logging in do_POST/do_GET


if __name__ == '__main__':
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
    print("  RobotConsole Mock Server (Pure Python - No Dependencies)")
    print("=" * 70)
    print("\n🌐 Network Configuration")
    print(f"  Server IP (localhost):   http://127.0.0.1:5000")
    print(f"  Server IP (network):     http://{local_ip}:5000  ← Use this for Vita3K")
    print("\nℹ️  For Vita3K on another machine:")
    print(f"  Configure robots.json with: \"{local_ip}\"")
    print("\nAvailable Endpoints:")
    print("  📨  POST http://localhost:5000/control       - Send control commands")
    print("  📊  GET  http://localhost:5000/status        - Get telemetry")
    print("  🏥  GET  http://localhost:5000/health        - Health check")
    print("  ℹ️   GET  http://localhost:5000/             - Server info")
    print("\nExample (from another machine):")
    print('  curl http://<server_ip>:5000/status')
    print("\n" + "=" * 70)
    print(f"Starting server on http://{local_ip}:5000 ...\n")
    
    # Start background updater
    update_thread = threading.Thread(target=background_update, daemon=True)
    update_thread.start()
    
    # Start HTTP server
    server = HTTPServer(('0.0.0.0', 5000), RobotConsoleHandler)
    
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        server.shutdown()
