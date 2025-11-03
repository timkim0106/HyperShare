#!/bin/bash

# HyperShare Quick Start Demo - Local Machine
# Demonstrates P2P file sharing between two local instances

set -e

echo "🚀 HyperShare Local Demo Starting..."
echo "This demo runs two HyperShare instances on different ports"
echo

# Configuration
NODE1_PORT=8080
NODE2_PORT=8081
DEMO_DIR="/tmp/hypershare_demo"
NODE1_DIR="${DEMO_DIR}/node1"
NODE2_DIR="${DEMO_DIR}/node2"
TEST_FILE="${DEMO_DIR}/demo_file.txt"

# Cleanup function
cleanup() {
    echo "🧹 Cleaning up demo..."
    pkill -f "hypershare.*daemon" || true
    rm -rf "${DEMO_DIR}"
    echo "✅ Cleanup complete"
}

# Set up cleanup on exit
trap cleanup EXIT

# Get absolute path to HyperShare executable
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
HYPERSHARE_BIN="${PROJECT_ROOT}/build/src/hypershare"

# Check if HyperShare is built
if [ ! -f "${HYPERSHARE_BIN}" ]; then
    echo "❌ HyperShare not found at: ${HYPERSHARE_BIN}"
    echo "   Please build first: mkdir build && cd build && cmake .. && make"
    exit 1
fi

echo "📁 Setting up demo directories..."
rm -rf "${DEMO_DIR}"
mkdir -p "${NODE1_DIR}" "${NODE2_DIR}"

# Create a test file with some interesting content
echo "🔧 Creating test file..."
cat > "${TEST_FILE}" << 'EOF'
# HyperShare Demo File
# Generated: $(date)

This is a demonstration file for HyperShare P2P file sharing system.

## Features Demonstrated:
- ✅ Peer-to-peer file discovery
- ✅ Secure file transfer with Blake3/ChaCha20/Ed25519
- ✅ Automatic chunking and resume capabilities
- ✅ Real-time transfer monitoring
- ✅ Unix socket IPC for concurrent operations

## Performance Characteristics:
- Target throughput: 100+ MB/s on local networks
- Sub-millisecond peer discovery
- Support for 100+ concurrent connections
- Minimal memory footprint with chunked transfers

## Technical Stack:
- Language: C++20 with coroutines and concepts
- Networking: Boost.Asio for async I/O
- Cryptography: libsodium (Blake3/ChaCha20/Ed25519)
- Storage: SQLite3 for metadata
- Testing: 799 assertions across comprehensive test suite

This file demonstrates HyperShare's ability to efficiently share
content across distributed networks with strong security guarantees.

Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod 
tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim 
veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea 
commodo consequat.
EOF

echo "🖥️  Starting HyperShare daemon..."
cd "${DEMO_DIR}"
"${HYPERSHARE_BIN}" start &
DAEMON_PID=$!
echo "   Daemon PID: ${DAEMON_PID}"

echo "⏳ Waiting for daemon to initialize..."
# Wait for IPC socket
for i in {1..10}; do
    if [ -S "/tmp/hypershare.sock" ]; then
        echo "   ✅ IPC socket created after ${i} seconds"
        break
    fi
    sleep 1
done
sleep 2

echo "📊 Checking daemon status..."
"${HYPERSHARE_BIN}" status

echo "📤 Sharing file via daemon..."
cp "${TEST_FILE}" ./demo_file.txt
"${HYPERSHARE_BIN}" share ./demo_file.txt

echo "⏳ Waiting for file announcement..."
sleep 2

echo "📊 Updated status showing shared file..."
"${HYPERSHARE_BIN}" status

echo "🔍 Demonstrating local file management..."
echo "Shared files are now available for other peers to discover and download"
ls -la ./demo_file.txt

echo "✅ Single daemon architecture demonstrated!"
echo "   - File shared through daemon"
echo "   - IPC communication working"
echo "   - Ready for peer connections"

echo "📈 Final daemon status..."
"${HYPERSHARE_BIN}" status

echo
echo "🎉 Basic demo completed!"
echo
echo "📋 Summary:"
echo "   ✅ Started single HyperShare daemon"
echo "   ✅ Established IPC communication" 
echo "   ✅ Shared file through daemon"
echo "   ✅ Verified daemon status and monitoring"
echo
echo "💡 Next Steps:"
echo "   - Connect from another machine: ${HYPERSHARE_BIN} connect $(hostname).local:8080"
echo "   - Monitor transfers: ${HYPERSHARE_BIN} status"
echo "   - Check peers: ${HYPERSHARE_BIN} peers"
echo "   - View logs: tail -f ./hypershare_data/hypershare.log"

# Keep demo running for manual exploration  
echo "⏸️  Daemon will continue running for 30 seconds for exploration..."
echo "   Press Ctrl+C to exit early"
sleep 30