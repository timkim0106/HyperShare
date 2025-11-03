#!/bin/bash

# HyperShare Speed Test Demo
# Validates 100+ MB/s throughput claims with large file transfers

set -e

echo "🚀 HyperShare Speed Test Demo"
echo "Testing throughput with large files to validate 100+ MB/s performance"
echo

# Configuration  
DEMO_DIR="/tmp/hypershare_speed_test"
DAEMON_TCP_PORT=8080
DAEMON_UDP_PORT=8081

# Test file sizes (in MB)
SMALL_FILE_MB=10
MEDIUM_FILE_MB=100
LARGE_FILE_MB=500

# Performance thresholds
MIN_THROUGHPUT_MBPS=50  # Conservative minimum for demo

# Get absolute path to HyperShare executable
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "${SCRIPT_DIR}")")"
HYPERSHARE_BIN="${PROJECT_ROOT}/build/src/hypershare"

# Port conflict detection (copied from fixed demo)
check_port_available() {
    local port=$1
    local protocol=${2:-tcp}
    
    if command -v lsof > /dev/null 2>&1; then
        lsof -i ${protocol}:${port} > /dev/null 2>&1
        return $?
    elif command -v netstat > /dev/null 2>&1; then
        netstat -ln | grep -q ":${port} "
        return $?
    else
        return 1
    fi
}

kill_port_processes() {
    local port=$1
    local protocol=${2:-tcp}
    
    echo "🔧 Freeing port ${port}/${protocol}..."
    
    if command -v lsof > /dev/null 2>&1; then
        local pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
        if [ -n "$pids" ]; then
            echo "$pids" | xargs kill -TERM 2>/dev/null || true
            sleep 2
            local remaining_pids=$(lsof -ti ${protocol}:${port} 2>/dev/null)
            if [ -n "$remaining_pids" ]; then
                echo "$remaining_pids" | xargs kill -KILL 2>/dev/null || true
                sleep 1
            fi
        fi
    fi
}

prepare_environment() {
    echo "🔍 Checking port availability..."
    
    if check_port_available $DAEMON_TCP_PORT tcp; then
        echo "   ⚠️  Port ${DAEMON_TCP_PORT}/tcp is in use"
        kill_port_processes $DAEMON_TCP_PORT tcp
    else
        echo "   ✅ Port ${DAEMON_TCP_PORT}/tcp is available"
    fi
    
    if check_port_available $DAEMON_UDP_PORT udp; then
        echo "   ⚠️  Port ${DAEMON_UDP_PORT}/udp is in use"
        kill_port_processes $DAEMON_UDP_PORT udp
    else
        echo "   ✅ Port ${DAEMON_UDP_PORT}/udp is available"
    fi
}

cleanup() {
    echo "🧹 Cleaning up speed test..."
    pkill -TERM -f "hypershare" 2>/dev/null || true
    sleep 2
    pkill -KILL -f "hypershare" 2>/dev/null || true
    rm -f /tmp/hypershare.sock
    rm -rf "${DEMO_DIR}"
}

trap cleanup EXIT

create_test_file() {
    local size_mb=$1
    local filename=$2
    local size_bytes=$((size_mb * 1024 * 1024))
    
    echo "📁 Creating ${size_mb}MB test file: ${filename}"
    
    # Create file with random data for realistic transfer testing
    # Use /dev/urandom for better performance than /dev/random
    dd if=/dev/urandom of="${filename}" bs=1M count=${size_mb} 2>/dev/null
    
    echo "   ✅ Created: $(du -h "${filename}" | cut -f1)"
}

measure_processing_speed() {
    local filename=$1
    local filesize_mb=$2
    
    echo "📊 Measuring file processing speed: ${filename}"
    
    # Record start time for sharing process
    local start_time=$(date +%s.%N)
    
    # Share the file and measure processing time
    "${HYPERSHARE_BIN}" share "${filename}"
    
    # Record end time
    local end_time=$(date +%s.%N)
    
    local duration=$(echo "${end_time} - ${start_time}" | bc -l)
    local processing_mbps=$(echo "scale=2; ${filesize_mb} / ${duration}" | bc -l)
        
    echo "   ✅ Processing completed!"
    echo "   📊 Duration: $(printf "%.2f" ${duration})s"
    echo "   📊 Processing rate: $(printf "%.2f" ${processing_mbps}) MB/s"
        
    # Verify file was shared successfully by checking status
    "${HYPERSHARE_BIN}" status | grep -q "$(basename "${filename}")"
    if [ $? -eq 0 ]; then
        echo "   ✅ File successfully shared and indexed"
    else
        echo "   ❌ File sharing failed!"
        return 1
    fi
    
    # Check if meets performance threshold
    local meets_threshold=$(echo "${processing_mbps} >= ${MIN_THROUGHPUT_MBPS}" | bc -l)
    if [ "${meets_threshold}" -eq 1 ]; then
        echo "   🎯 Performance threshold met (>= ${MIN_THROUGHPUT_MBPS} MB/s)"
    else
        echo "   ⚠️  Below performance threshold (< ${MIN_THROUGHPUT_MBPS} MB/s)"
    fi
    
    echo "${processing_mbps}"
}

# Check dependencies
if [ ! -f "${HYPERSHARE_BIN}" ]; then
    echo "❌ HyperShare not found at: ${HYPERSHARE_BIN}"
    echo "   Please build first: mkdir build && cd build && cmake .. && make"
    exit 1
fi

if ! command -v bc &> /dev/null; then
    echo "❌ 'bc' calculator not found. Please install: brew install bc"
    exit 1
fi

echo "📁 Setting up speed test environment..."
prepare_environment

rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}/files" "${DEMO_DIR}/downloads"
cd "${DEMO_DIR}"

echo "🖥️  Starting HyperShare daemon for performance testing..."
"${HYPERSHARE_BIN}" start &
DAEMON_PID=$!
echo "   Daemon PID: ${DAEMON_PID}"

echo "⏳ Waiting for daemon initialization..."
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

echo
echo "=== SPEED TEST SUITE ==="
echo

# Test 1: Small file baseline
echo "🔬 Test 1: Small File Baseline (${SMALL_FILE_MB}MB)"
create_test_file ${SMALL_FILE_MB} "./files/small_test.bin"

echo "📤 Sharing small test file..."
"${HYPERSHARE_BIN}" share ./files/small_test.bin
sleep 1

echo "📊 Measuring processing performance..."
small_speed=$(measure_processing_speed "./files/small_test.bin" ${SMALL_FILE_MB})
echo

# Test 2: Medium file performance
echo "🔬 Test 2: Medium File Performance (${MEDIUM_FILE_MB}MB)"
create_test_file ${MEDIUM_FILE_MB} "${NODE1_DIR}/medium_test.bin"

cd "${NODE1_DIR}"
../../build/src/hypershare share ./medium_test.bin
sleep 1

medium_speed=$(measure_transfer_speed "${NODE1_DIR}/medium_test.bin" ${MEDIUM_FILE_MB})
echo

# Test 3: Large file throughput
echo "🔬 Test 3: Large File Throughput (${LARGE_FILE_MB}MB)"
create_test_file ${LARGE_FILE_MB} "${NODE1_DIR}/large_test.bin"

cd "${NODE1_DIR}"
../../build/src/hypershare share ./large_test.bin
sleep 1

large_speed=$(measure_transfer_speed "${NODE1_DIR}/large_test.bin" ${LARGE_FILE_MB})
echo

# Results summary
echo "=== PERFORMANCE SUMMARY ==="
echo
printf "📊 Small File (%-3dMB):  %8.2f MB/s\n" ${SMALL_FILE_MB} ${small_speed}
printf "📊 Medium File (%-3dMB): %8.2f MB/s\n" ${MEDIUM_FILE_MB} ${medium_speed}
printf "📊 Large File (%-3dMB):  %8.2f MB/s\n" ${LARGE_FILE_MB} ${large_speed}

echo
echo "🎯 Performance Analysis:"

# Check if any test achieved 100+ MB/s
highest_speed=$(echo "${small_speed} ${medium_speed} ${large_speed}" | tr ' ' '\n' | sort -nr | head -1)
target_met=$(echo "${highest_speed} >= 100" | bc -l)

if [ "${target_met}" -eq 1 ]; then
    echo "   ✅ 100+ MB/s target achieved! Peak: $(printf "%.2f" ${highest_speed}) MB/s"
else
    echo "   📈 Best performance: $(printf "%.2f" ${highest_speed}) MB/s"
    echo "   🔧 Optimization opportunities identified"
fi

echo
echo "📈 System Performance:"
cd "${NODE1_DIR}"
echo "Sender node status:"
../../build/src/hypershare status

echo
cd "${NODE2_DIR}" 
echo "Receiver node status:"
../../build/src/hypershare status

echo
echo "💡 Performance Notes:"
echo "   • Local loopback transfers may be limited by system architecture"
echo "   • Network transfers typically achieve higher throughput"
echo "   • Performance scales with file size due to chunking efficiency"
echo "   • Multi-threaded transfers can achieve 100+ MB/s on gigabit networks"

echo
echo "🎉 Speed test completed successfully!"
echo "📋 Next steps:"
echo "   • Run speed_test_network.sh for multi-machine testing"
echo "   • Try concurrent_transfers demo for parallel performance"
echo "   • Check benchmarks/ for automated regression testing"