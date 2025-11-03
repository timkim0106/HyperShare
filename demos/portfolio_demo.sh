#!/bin/bash

# HyperShare Portfolio Demo Script
# Designed for screen recording to create professional YouTube demo video
# Shows terminal-to-terminal P2P file transfer with clear narration cues

set -e

echo "🎬 HyperShare Portfolio Demo"
echo "=========================================="
echo "This demo showcases P2P file sharing between two terminals"
echo "Perfect for recording a professional demonstration video"
echo
echo "📋 Demo Flow:"
echo "   1. Setup two terminal sessions"
echo "   2. Start daemon in Terminal 1" 
echo "   3. Connect from Terminal 2"
echo "   4. Share file from Terminal 1"
echo "   5. Discover and download in Terminal 2"
echo "   6. Show cryptographic verification"
echo

# Configuration
DEMO_DIR="/tmp/hypershare_portfolio_demo"
DEMO_FILE="${DEMO_DIR}/portfolio_sample.pdf"
DAEMON_TCP_PORT=8080
DAEMON_UDP_PORT=8081
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
HYPERSHARE_BIN="${PROJECT_ROOT}/build/src/hypershare"

# Port conflict detection and resolution
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
    echo "🔍 Ensuring clean environment for recording..."
    
    if check_port_available $DAEMON_TCP_PORT tcp; then
        echo "   Freeing port ${DAEMON_TCP_PORT}/tcp..."
        kill_port_processes $DAEMON_TCP_PORT tcp
    fi
    
    if check_port_available $DAEMON_UDP_PORT udp; then
        echo "   Freeing port ${DAEMON_UDP_PORT}/udp..."
        kill_port_processes $DAEMON_UDP_PORT udp
    fi
    
    # Clean any existing sockets/processes
    pkill -f "hypershare" 2>/dev/null || true
    rm -f /tmp/hypershare.sock
    
    echo "   ✅ Environment ready for demo recording"
}

# Enhanced cleanup function
cleanup() {
    echo "🧹 Cleaning up demo..."
    pkill -TERM -f "hypershare" 2>/dev/null || true
    sleep 2
    pkill -KILL -f "hypershare" 2>/dev/null || true
    
    # Clean up ports if needed
    if check_port_available $DAEMON_TCP_PORT tcp; then
        kill_port_processes $DAEMON_TCP_PORT tcp
    fi
    if check_port_available $DAEMON_UDP_PORT udp; then
        kill_port_processes $DAEMON_UDP_PORT udp
    fi
    
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

# Prepare demo environment
echo "📁 Preparing demo environment..."
prepare_environment

rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}"
cd "${DEMO_DIR}"

# Create a realistic demo file that looks portfolio-worthy
echo "📄 Creating realistic demo file..."
cat > "${DEMO_FILE}" << 'EOF'
# HyperShare: Distributed P2P File Sharing System
## Technical Portfolio Demonstration

### System Architecture
- **Language**: C++20 with coroutines and concepts
- **Networking**: Boost.Asio for asynchronous I/O
- **Cryptography**: libsodium (Blake3/ChaCha20/Ed25519)
- **IPC**: Unix domain sockets for daemon communication
- **Storage**: SQLite for metadata and file indexing

### Key Features Demonstrated
1. **Peer Discovery**: UDP multicast announcement (239.255.42.99:8081)
2. **Secure Handshake**: Ed25519 key exchange and verification
3. **Encrypted Transfer**: ChaCha20-Poly1305 authenticated encryption
4. **File Integrity**: Blake3 cryptographic hashing
5. **Resume Capability**: Chunked transfers with automatic resume
6. **Concurrent Operations**: Multiple CLI clients via IPC

### Network Protocol Flow
```
Peer A                           Peer B
  |                                |
  |-- UDP Discovery Broadcast ---->|
  |<--- UDP Discovery Response ----|
  |                                |
  |-- TCP Connection Request ----->|
  |<--- Ed25519 Handshake --------|
  |-- File Announcement --------->|
  |<--- Download Request ---------|
  |-- Encrypted File Chunks ----->|
  |<--- Blake3 Verification ------|
```

### Performance Characteristics
- **Target Throughput**: 100+ MB/s on gigabit networks
- **Discovery Latency**: Sub-millisecond peer detection
- **Memory Efficiency**: Streaming transfers with 64KB chunks
- **Concurrent Connections**: Supports 100+ simultaneous peers

### Security Model
- **Authentication**: Ed25519 digital signatures (256-bit)
- **Encryption**: ChaCha20-Poly1305 AEAD (256-bit keys)
- **Integrity**: Blake3 hash verification (256-bit)
- **Forward Secrecy**: Ephemeral key exchange per session

This file demonstrates HyperShare's ability to efficiently and securely 
share content across distributed networks using modern cryptographic 
protocols and high-performance C++ implementation.

### Implementation Highlights
- Clean separation of network/storage/crypto layers
- Professional daemon architecture with Unix socket IPC
- Comprehensive test suite with 799+ assertions
- Modern C++20 features: coroutines, concepts, ranges
- Production-ready error handling and resource management

File Size: ~2KB for quick demonstration
Generated: $(date)
EOF

# Calculate file size for display
FILE_SIZE=$(du -h "${DEMO_FILE}" | cut -f1)

echo "✅ Demo file created: ${FILE_SIZE}"
echo

echo "🎯 RECORDING INSTRUCTIONS FOR YOUTUBE VIDEO"
echo "============================================="
echo
echo "📺 TERMINAL SETUP:"
echo "   1. Split your screen or use two terminal windows"
echo "   2. Terminal 1 (LEFT): File Sharer (this script continues here)"
echo "   3. Terminal 2 (RIGHT): File Receiver (you'll run commands manually)"
echo "   4. Make sure both terminals are visible in recording"
echo
echo "🎙️  NARRATION SCRIPT:"
echo "   'I'm demonstrating HyperShare, a P2P file sharing system I built with C++20.'"
echo "   'It uses UDP for peer discovery and secure crypto for file transfers.'"
echo "   'On the left, I'm starting the daemon. On the right, I'll connect as a peer.'"
echo
echo "⏯️  PAUSE HERE TO START RECORDING"
echo "   Press ENTER when you're ready to begin the demo..."
read -p ""

echo
echo "🖥️  TERMINAL 1 (FILE SHARER) - Starting daemon..."
echo "   💬 SAY: 'First, I start the HyperShare daemon'"

# Start daemon 
"${HYPERSHARE_BIN}" start &
DAEMON_PID=$!
echo "   Daemon PID: ${DAEMON_PID}"

echo "⏳ Waiting for daemon initialization..."
sleep 3

echo
echo "📊 Checking daemon status..."
echo "   💬 SAY: 'The daemon is now running and ready for peer connections'"
"${HYPERSHARE_BIN}" status

echo
echo "📤 Sharing the demo file..."
echo "   💬 SAY: 'Now I share a file - it gets announced to the network'"
cp "${DEMO_FILE}" ./shared_portfolio_demo.pdf
"${HYPERSHARE_BIN}" share ./shared_portfolio_demo.pdf

echo
echo "📊 Status after file sharing..."
"${HYPERSHARE_BIN}" status

echo
echo "🔗 TERMINAL 2 INSTRUCTIONS:"
echo "============================================="
echo "   💬 SAY: 'On the right terminal, I'll connect as a second peer'"
echo
echo "   🖥️  Run these commands in TERMINAL 2 (RIGHT):"
echo "   1. cd ${DEMO_DIR}"
echo "   2. ${HYPERSHARE_BIN} start"
echo "   3. ${HYPERSHARE_BIN} connect localhost:${DAEMON_TCP_PORT}"
echo "   4. ${HYPERSHARE_BIN} list"
echo "   5. ${HYPERSHARE_BIN} download shared_portfolio_demo.pdf"
echo "   6. ls -la *.pdf"
echo
echo "   💬 EXPLAIN EACH STEP:"
echo "   - 'Starting second peer daemon'"
echo "   - 'Connecting to the first peer on port ${DAEMON_TCP_PORT}'"
echo "   - 'Discovering available files through UDP announcement'"
echo "   - 'Downloading with Blake3 verification and ChaCha20 encryption'"
echo "   - 'File successfully transferred and verified'"
echo

echo "⏸️  PAUSE FOR TERMINAL 2 DEMO"
echo "   Complete the Terminal 2 steps above, then press ENTER to continue..."
read -p ""

echo
echo "📊 Final status check in Terminal 1..."
echo "   💬 SAY: 'Back in Terminal 1, we can see the transfer statistics'"
"${HYPERSHARE_BIN}" status

echo
echo "🔍 Peer connection status..."
echo "   💬 SAY: 'And here are the active peer connections'"
"${HYPERSHARE_BIN}" peers

echo
echo "🎉 DEMO COMPLETE!"
echo "============================================="
echo "   💬 CLOSING NARRATION:"
echo "   'This demonstrates HyperShare's core features:'"
echo "   '• Automatic peer discovery via UDP multicast'"
echo "   '• Secure file transfers with modern cryptography'"
echo "   '• Professional daemon architecture with Unix socket IPC'"
echo "   '• Real-time monitoring and concurrent operations'"
echo "   'The system is built with C++20 and includes 799+ test assertions'"
echo "   'for production-ready reliability.'"

echo
echo "📝 VIDEO EDITING NOTES:"
echo "   - Show both terminals clearly throughout"
echo "   - Highlight the key commands and their output"
echo "   - Consider adding text overlays for technical terms"
echo "   - Keep total video length 2-3 minutes for optimal engagement"
echo "   - End with a brief architecture diagram or code snippet"

echo
echo "⏸️  Demo will continue running for 30 seconds for final shots..."
echo "   Press Ctrl+C to exit early"
sleep 30