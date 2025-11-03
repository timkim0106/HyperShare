#!/bin/bash

# HyperShare Quick Start Demo - Fixed for Daemon + IPC Architecture
# Demonstrates the professional daemon architecture with Unix socket IPC

set -e

echo "🚀 HyperShare Demo: Daemon + IPC Architecture"
echo "Showcasing single daemon with concurrent CLI operations via Unix socket"
echo

# Configuration
DEMO_DIR="/tmp/hypershare_demo"
TEST_FILE="${DEMO_DIR}/demo_file.txt"
DAEMON_TCP_PORT=8080
DAEMON_UDP_PORT=8081

# Get absolute path to HyperShare executable
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
HYPERSHARE_BIN="${PROJECT_ROOT}/build/src/hypershare"

# Port conflict detection and resolution
check_port_available() {
    local port=$1
    local protocol=${2:-tcp}
    
    if command -v lsof > /dev/null 2>&1; then
        # Using lsof (most reliable)
        lsof -i ${protocol}:${port} > /dev/null 2>&1
        return $?
    elif command -v netstat > /dev/null 2>&1; then
        # Fallback to netstat
        netstat -ln | grep -q ":${port} "
        return $?
    else
        # Last resort: try to bind (less reliable)
        python3 -c "
import socket
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM if '$protocol' == 'tcp' else socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', $port))
    s.close()
    exit(1)  # Port is available
except:
    exit(0)  # Port is in use
" 2>/dev/null
        return $?
    fi
}

kill_port_processes() {
    local port=$1
    local protocol=${2:-tcp}
    
    echo "🔧 Attempting to free port ${port}/${protocol}..."
    
    if command -v lsof > /dev/null 2>&1; then
        local pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
        if [ -n "$pids" ]; then
            echo "   Found processes using port ${port}: $pids"
            echo "   Sending TERM signal..."
            echo "$pids" | xargs kill -TERM 2>/dev/null || true
            sleep 2
            
            # Check if still running, force kill if needed
            local remaining_pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
            if [ -n "$remaining_pids" ]; then
                echo "   Force killing remaining processes: $remaining_pids"
                echo "$remaining_pids" | xargs kill -KILL 2>/dev/null || true
                sleep 1
            fi
        fi
    fi
}

prepare_environment() {
    echo "🔍 Checking port availability..."
    
    # Check and free TCP port
    if check_port_available $DAEMON_TCP_PORT tcp; then
        echo "   ⚠️  Port ${DAEMON_TCP_PORT}/tcp is in use"
        kill_port_processes $DAEMON_TCP_PORT tcp
        
        # Verify it's now free
        if check_port_available $DAEMON_TCP_PORT tcp; then
            echo "   ❌ Failed to free port ${DAEMON_TCP_PORT}/tcp"
            echo "   Please manually kill processes using this port:"
            echo "   lsof -ti tcp:${DAEMON_TCP_PORT} | xargs kill"
            exit 1
        else
            echo "   ✅ Port ${DAEMON_TCP_PORT}/tcp freed successfully"
        fi
    else
        echo "   ✅ Port ${DAEMON_TCP_PORT}/tcp is available"
    fi
    
    # Check and free UDP port  
    if check_port_available $DAEMON_UDP_PORT udp; then
        echo "   ⚠️  Port ${DAEMON_UDP_PORT}/udp is in use"
        kill_port_processes $DAEMON_UDP_PORT udp
        
        # Verify it's now free
        if check_port_available $DAEMON_UDP_PORT udp; then
            echo "   ❌ Failed to free port ${DAEMON_UDP_PORT}/udp"
            echo "   Please manually kill processes using this port:"
            echo "   lsof -ti udp:${DAEMON_UDP_PORT} | xargs kill"
            exit 1
        else
            echo "   ✅ Port ${DAEMON_UDP_PORT}/udp freed successfully"
        fi
    else
        echo "   ✅ Port ${DAEMON_UDP_PORT}/udp is available"
    fi
    
    echo "   ✅ All required ports are available"
}

# Enhanced cleanup function
cleanup() {
    echo "🧹 Cleaning up demo..."
    
    # Kill hypershare processes gracefully first
    echo "   Terminating HyperShare processes..."
    pkill -TERM -f "hypershare" 2>/dev/null || true
    
    # Wait for graceful shutdown
    sleep 3
    
    # Force kill any remaining processes
    pkill -KILL -f "hypershare" 2>/dev/null || true
    
    # Free up ports explicitly if needed
    if check_port_available $DAEMON_TCP_PORT tcp; then
        echo "   Freeing TCP port ${DAEMON_TCP_PORT}..."
        kill_port_processes $DAEMON_TCP_PORT tcp
    fi
    
    if check_port_available $DAEMON_UDP_PORT udp; then
        echo "   Freeing UDP port ${DAEMON_UDP_PORT}..."
        kill_port_processes $DAEMON_UDP_PORT udp
    fi
    
    # Remove IPC socket
    rm -f /tmp/hypershare.sock
    
    # Clean demo directory
    rm -rf "${DEMO_DIR}"
    
    echo "   ✅ Cleanup complete"
}

# Set up cleanup on exit
trap cleanup EXIT

# Check if HyperShare is built
if [ ! -f "${HYPERSHARE_BIN}" ]; then
    echo "❌ HyperShare not found at: ${HYPERSHARE_BIN}"
    echo "   Please build first: mkdir build && cd build && cmake .. && make"
    exit 1
fi

echo "📁 Setting up demo environment..."

# Prepare environment and check ports
prepare_environment

# Clean any existing processes/sockets first
echo "   Ensuring clean start..."
pkill -f "hypershare" 2>/dev/null || true
rm -f /tmp/hypershare.sock
sleep 1

rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}"
cd "${DEMO_DIR}"

# Create a demonstration file
echo "🔧 Creating demonstration file..."
cat > "${TEST_FILE}" << 'EOF'
# HyperShare Daemon Architecture Demo

This file demonstrates HyperShare's professional architecture:

## System Design
- ✅ Single daemon process (efficient resource usage)
- ✅ Unix socket IPC (/tmp/hypershare.sock)
- ✅ Concurrent CLI operations
- ✅ Real-time network monitoring

## Network Features
- P2P discovery via UDP multicast (239.255.42.99)
- Secure file transfers with Blake3/ChaCha20/Ed25519
- Automatic peer announcement and discovery
- Chunked file transfers with resume capability

## Professional Architecture Benefits
- Centralized state management
- Efficient connection handling
- Clean separation of network/UI concerns
- Industry-standard daemon design

Generated: $(date)
File size: ~1KB for quick transfer demo
EOF

echo "📊 File created: $(du -h "${TEST_FILE}" | cut -f1)"
echo

echo "🖥️  Starting HyperShare daemon..."
echo "   This starts the core network service on standard ports:"
echo "   • TCP: 8080 (peer connections)"
echo "   • UDP: 8081 (peer discovery)"
echo "   • IPC: /tmp/hypershare.sock (CLI communication)"
echo

# Start daemon in background
"${HYPERSHARE_BIN}" start &
DAEMON_PID=$!
echo "   Daemon PID: ${DAEMON_PID}"

echo "⏳ Waiting for daemon initialization..."
echo "   Checking for IPC socket creation..."

# Wait for IPC socket to be created
for i in {1..10}; do
    if [ -S "/tmp/hypershare.sock" ]; then
        echo "   ✅ IPC socket created after ${i} seconds"
        break
    fi
    echo "   ⏳ Waiting for IPC socket... (${i}/10)"
    sleep 1
done

# Give extra time for full daemon startup
sleep 2

echo
echo "📊 Checking daemon status via IPC..."
"${HYPERSHARE_BIN}" status

echo
echo "🔍 Checking peer discovery..."
"${HYPERSHARE_BIN}" peers

echo
echo "📤 Sharing file via CLI → IPC → Daemon..."
cp "${TEST_FILE}" ./shared_demo.txt
"${HYPERSHARE_BIN}" share ./shared_demo.txt

echo
echo "⏳ Waiting for file announcement to network..."
sleep 2

echo "📊 Updated status showing shared file..."
"${HYPERSHARE_BIN}" status

echo
echo "🎯 Demonstrating Concurrent CLI Operations"
echo "   The daemon continues running while we execute multiple CLI commands:"

echo
echo "   Command 1: List peers"
"${HYPERSHARE_BIN}" peers

echo
echo "   Command 2: Check status again"
"${HYPERSHARE_BIN}" status

echo
echo "   Command 3: View available files"
echo "   Files available for download:"
ls -la *.txt

echo
echo "🔗 Network Architecture Summary:"
echo "   ✅ Daemon running: PID ${DAEMON_PID}"
echo "   ✅ TCP server: localhost:${DAEMON_TCP_PORT}"
echo "   ✅ UDP discovery: port ${DAEMON_UDP_PORT}, multicast 239.255.42.99"
echo "   ✅ IPC server: /tmp/hypershare.sock"
echo "   ✅ CLI operations: All working via IPC"
echo "   ✅ Port conflicts: Automatically resolved"

echo
echo "💡 Multi-Machine Setup:"
echo "   To connect from another machine:"
echo "   1. Start daemon: ${HYPERSHARE_BIN} start"
echo "   2. Connect to this machine: ${HYPERSHARE_BIN} connect $(hostname).local:${DAEMON_TCP_PORT}"
echo "   3. Download files: ${HYPERSHARE_BIN} download shared_demo.txt"

echo
echo "🔬 Advanced Features Demonstrated:"
echo "   • Professional daemon architecture (like SSH, Docker, PostgreSQL)"
echo "   • Unix socket IPC for efficient local communication"
echo "   • Concurrent operations without blocking daemon"
echo "   • Real-time network state monitoring"
echo "   • Industry-standard single-process design"

echo
echo "🎉 Demo completed successfully!"
echo
echo "📋 Architecture Highlights:"
echo "   ✅ Single daemon handling all network operations"
echo "   ✅ Multiple concurrent CLI clients via IPC"
echo "   ✅ Clean separation of network/interface concerns"
echo "   ✅ Efficient resource usage (1 process vs N processes)"
echo "   ✅ Professional system design following industry standards"

echo
echo "🔧 Continue experimenting:"
echo "   • Share more files: ${HYPERSHARE_BIN} share <filename>"
echo "   • Monitor in real-time: ${HYPERSHARE_BIN} status"
echo "   • Check peer discovery: ${HYPERSHARE_BIN} peers"
echo "   • View daemon logs: tail -f ./hypershare_data/hypershare.log"

echo
echo "⏸️  Daemon will continue running for 30 seconds for manual exploration..."
echo "   Press Ctrl+C to exit early"

# Let user explore the running system
sleep 30

echo
echo "🏁 Demo ending - daemon will be terminated during cleanup"