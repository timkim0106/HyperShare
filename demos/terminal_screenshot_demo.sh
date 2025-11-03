#!/bin/bash

# HyperShare Terminal Screenshot Demo
# Creates perfect setup for portfolio screenshots showing two terminals
# Terminal 1: File sharing    Terminal 2: File downloading

set -e

echo "📸 HyperShare Terminal Screenshot Demo Setup"
echo "=============================================="
echo
echo "This script sets up the perfect scenario for portfolio screenshots"
echo "showing HyperShare P2P file transfer between two terminals."
echo

# Configuration
DEMO_DIR="/tmp/hypershare_screenshot_demo"
DAEMON_TCP_PORT=8080
DAEMON_UDP_PORT=8081

# Get absolute path to HyperShare executable
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
HYPERSHARE_BIN="${PROJECT_ROOT}/build/src/hypershare"

# Port conflict detection
check_port_available() {
    local port=$1
    local protocol=${2:-tcp}
    
    if command -v lsof > /dev/null 2>&1; then
        lsof -i ${protocol}:${port} > /dev/null 2>&1
        return $?
    else
        return 1
    fi
}

kill_port_processes() {
    local port=$1
    local protocol=${2:-tcp}
    
    if command -v lsof > /dev/null 2>&1; then
        local pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
        if [ -n "$pids" ]; then
            echo "$pids" | xargs kill -TERM 2>/dev/null || true
            sleep 1
            local remaining_pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
            if [ -n "$remaining_pids" ]; then
                echo "$remaining_pids" | xargs kill -KILL 2>/dev/null || true
            fi
        fi
    fi
}

prepare_environment() {
    echo "🔍 Ensuring clean environment..."
    
    if check_port_available $DAEMON_TCP_PORT tcp; then
        echo "   Freeing port ${DAEMON_TCP_PORT}/tcp..."
        kill_port_processes $DAEMON_TCP_PORT tcp
    fi
    
    if check_port_available $DAEMON_UDP_PORT udp; then
        echo "   Freeing port ${DAEMON_UDP_PORT}/udp..."  
        kill_port_processes $DAEMON_UDP_PORT udp
    fi
    
    echo "   ✅ Environment ready"
}

cleanup() {
    echo "🧹 Cleaning up screenshot demo..."
    pkill -TERM -f "hypershare" 2>/dev/null || true
    sleep 2
    pkill -KILL -f "hypershare" 2>/dev/null || true
    rm -f /tmp/hypershare.sock
    rm -rf "${DEMO_DIR}"
    echo "✅ Cleanup complete"
}

trap cleanup EXIT

# Check if HyperShare is built
if [ ! -f "${HYPERSHARE_BIN}" ]; then
    echo "❌ HyperShare not found at: ${HYPERSHARE_BIN}"
    echo "   Please build first: mkdir build && cd build && cmake .. && make"
    exit 1
fi

echo "📁 Setting up demo environment..."
prepare_environment

rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}/terminal1" "${DEMO_DIR}/terminal2"

# Create an attractive demo file for screenshots
echo "📄 Creating demo file for screenshots..."
cat > "${DEMO_DIR}/portfolio_document.pdf" << 'EOF'
# HyperShare Portfolio Demonstration

## Project Overview
This document demonstrates HyperShare's P2P file sharing capabilities.

**Key Features:**
✅ Peer-to-peer file discovery and transfer
✅ Secure cryptography (Ed25519/ChaCha20/Blake3)  
✅ Real-time transfer monitoring
✅ Professional daemon architecture with Unix socket IPC
✅ High-performance async I/O with C++20

**Performance Metrics:**
• Target throughput: 100+ MB/s on gigabit networks
• Discovery latency: Sub-millisecond peer detection
• Memory efficiency: Streaming transfers with chunked data
• Concurrent support: 100+ simultaneous connections

**Technical Implementation:**
• Language: C++20 with modern features (coroutines, concepts)
• Networking: Boost.Asio for asynchronous I/O operations
• Cryptography: libsodium providing military-grade security
• Storage: SQLite for metadata with efficient file indexing
• Testing: 799+ assertions across comprehensive test suite

**Architecture Highlights:**
• Single daemon process for efficient resource usage
• Unix domain socket IPC for local communication
• Clean separation of network, storage, and crypto layers
• Production-ready error handling and resource management

This file serves as a demonstration of HyperShare's ability to 
efficiently share content across distributed networks with 
strong security guarantees and professional-grade implementation.

File size: ~2KB for quick transfer demonstration
Generated: $(date)
Technology: C++20 P2P File Sharing System
EOF

# Calculate file size
FILE_SIZE=$(du -h "${DEMO_DIR}/portfolio_document.pdf" | cut -f1)
echo "   ✅ Demo file created: ${FILE_SIZE}"

echo
echo "🖥️  Starting HyperShare daemon..."
cd "${DEMO_DIR}"
"${HYPERSHARE_BIN}" start &
DAEMON_PID=$!
echo "   Daemon PID: ${DAEMON_PID}"

echo "⏳ Waiting for daemon initialization..."
for i in {1..10}; do
    if [ -S "/tmp/hypershare.sock" ]; then
        echo "   ✅ IPC socket ready after ${i} seconds"
        break
    fi
    sleep 1
done
sleep 2

echo
echo "📤 Sharing demo file..."
"${HYPERSHARE_BIN}" share ./portfolio_document.pdf

echo "⏳ Allowing file announcement..."
sleep 2

echo
echo "📊 Current status:"
"${HYPERSHARE_BIN}" status

echo
echo "📸 SCREENSHOT SETUP COMPLETE!"
echo "=============================================="
echo
echo "🎯 Perfect for Portfolio Screenshots:"
echo
echo "**TERMINAL 1 (LEFT SIDE) - File Sharer:**"
echo "   cd ${DEMO_DIR}/terminal1"
echo "   ${HYPERSHARE_BIN} status"
echo "   ${HYPERSHARE_BIN} share ../portfolio_document.pdf"
echo 
echo "**TERMINAL 2 (RIGHT SIDE) - File Receiver:**"
echo "   cd ${DEMO_DIR}/terminal2"  
echo "   ${HYPERSHARE_BIN} connect localhost:8080"
echo "   ${HYPERSHARE_BIN} list"
echo "   ${HYPERSHARE_BIN} download portfolio_document.pdf"
echo "   ls -la *.pdf"
echo
echo "💡 Screenshot Tips:"
echo "   • Use two terminal windows side-by-side"
echo "   • Increase font size for readability (14-16pt)"
echo "   • Use high contrast theme (dark background recommended)"
echo "   • Capture the moment showing successful transfer"
echo "   • Include the command prompts and outputs"
echo
echo "🎨 Recommended Terminal Settings:"
echo "   • Theme: Dark background with bright text"
echo "   • Font: SF Mono, Menlo, or Consolas"
echo "   • Size: 14-16pt for screenshot readability"
echo "   • Colors: High contrast for professional appearance"
echo
echo "📋 Screenshot Checklist:"
echo "   ✅ Both terminals visible and clearly labeled"
echo "   ✅ Commands and outputs are readable"
echo "   ✅ File transfer success is evident"
echo "   ✅ Professional terminal appearance"
echo "   ✅ No sensitive information visible"
echo
echo "⏸️  Daemon ready for screenshot session..."
echo "   Commands above are ready to run in separate terminals"
echo "   Press Ctrl+C when done taking screenshots"

# Keep running for screenshot session
echo "   Waiting for screenshot session... (Press Ctrl+C to exit)"
while true; do
    sleep 10
    echo "   📊 Status check: $(date +%H:%M:%S)"
    "${HYPERSHARE_BIN}" status | grep "Files shared" || echo "   Daemon running normally"
done