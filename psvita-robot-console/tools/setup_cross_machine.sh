#!/bin/bash

# RobotConsole Cross-Machine Setup Helper
# Helps configure Vita3K (on Machine A) to connect to Flask Server (on Machine B)

set -e

echo "=========================================="
echo "RobotConsole Cross-Machine Network Setup"
echo "=========================================="
echo ""

# Get local machine IP
get_local_ip() {
    # Try multiple methods
    local ip=$(hostname -I 2>/dev/null | awk '{print $1}')
    
    if [ -z "$ip" ]; then
        ip=$(ifconfig 2>/dev/null | grep -m1 "inet " | awk '{print $2}' | cut -d: -f2)
    fi
    
    if [ -z "$ip" ]; then
        ip=$(ip addr 2>/dev/null | grep -m1 "inet " | awk '{print $2}' | cut -d/ -f1)
    fi
    
    echo "$ip"
}

echo "🖥️  MACHINE CONFIGURATION"
echo "=========================================="
echo ""
echo "You have two machines:"
echo "  [Machine A] Running Vita3K emulator"
echo "  [Machine B] Running Flask mock server"
echo ""

read -p "Is THIS computer running the Flask server? (y/n): " is_server

if [ "$is_server" = "y" ] || [ "$is_server" = "Y" ]; then
    echo ""
    echo "📡 FLASK SERVER SETUP (THIS MACHINE)"
    echo "=========================================="
    
    local_ip=$(get_local_ip)
    
    if [ -z "$local_ip" ]; then
        echo "⚠️  Could not auto-detect IP address"
        echo ""
        echo "Manual options:"
        echo "  1. IPv4 address (e.g., 192.168.1.100)"
        echo "  2. Hostname (e.g., my-laptop.local)"
        echo "  3. localhost (if testing on same machine)"
        echo ""
        read -p "Enter your server IP/hostname: " local_ip
    else
        echo "✅ Detected your IP address: $local_ip"
        read -p "Is this correct? (y/n): " confirm
        if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
            read -p "Enter correct IP/hostname: " local_ip
        fi
    fi
    
    echo ""
    echo "📋 FOR THE VITA3K MACHINE"
    echo "=========================================="
    echo ""
    echo "Update robots.json with this IP address:"
    echo ""
    echo "  {\"robots\": [\"$local_ip\", \"$local_ip\", \"$local_ip\"]}"
    echo ""
    echo "Then:"
    echo "  1. Rebuild RobotConsole.vpk"
    echo "  2. Copy VPK to Vita3K machine"
    echo "  3. Start Flask server (this machine):"
    echo ""
    echo "python3 flask_mock_server.py"
    echo ""
    echo "4. Launch RobotConsole in Vita3K and connect"
    echo ""
    
else
    echo ""
    echo "🖥️  VITA3K SETUP (THIS MACHINE)"
    echo "=========================================="
    echo ""
    read -p "Enter Flask server IP address (e.g., 192.168.1.100): " server_ip
    
    if [ -z "$server_ip" ]; then
        echo "❌ No IP address provided"
        exit 1
    fi
    
    echo ""
    echo "✅ Robots Configuration"
    echo "=========================================="
    echo ""
    echo "Update assets/robots.json to:"
    echo ""
    echo "  {\"robots\": [\"$server_ip\", \"$server_ip\", \"$server_ip\"]}"
    echo ""
    echo "Then:"
    echo "  1. Rebuild RobotConsole.vpk"
    echo "  2. Start the app in Vita3K"
    echo ""
fi

echo ""
echo "🔗 NETWORK TROUBLESHOOTING"
echo "=========================================="
echo ""
echo "If the app can't connect:"
echo ""
echo "1. Verify both machines are on same network:"
echo "   ping $local_ip"
echo ""
echo "2. Check Flask server is running:"
echo "   curl http://<server_ip>:5000/health"
echo ""
echo "3. Check firewall allows port 5000:"
echo "   sudo ufw allow 5000"
echo "   (Linux) or disable firewall temporarily"
echo ""
echo "4. Verify IP address in robots.json:"
echo "   cat assets/robots.json"
echo ""
echo "5. Check server console for errors"
echo ""
echo "=========================================="
echo ""
