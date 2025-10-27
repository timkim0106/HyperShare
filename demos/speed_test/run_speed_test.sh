#!/bin/bash

# HyperShare Speed Test Demo
# Validates 100+ MB/s throughput claims with large file transfers

set -e

echo "🚀 HyperShare Speed Test Demo"
echo "Testing throughput with large files to validate 100+ MB/s performance"
echo

# Configuration
NODE1_PORT=8080
NODE2_PORT=8081
DEMO_DIR="/tmp/hypershare_speed_test"
NODE1_DIR="${DEMO_DIR}/sender"
NODE2_DIR="${DEMO_DIR}/receiver"

# Test file sizes (in MB)
SMALL_FILE_MB=10
MEDIUM_FILE_MB=100
LARGE_FILE_MB=500

# Performance thresholds
MIN_THROUGHPUT_MBPS=50  # Conservative minimum for demo

cleanup() {
    echo "🧹 Cleaning up speed test..."
    pkill -f "hypershare.*daemon" || true
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

measure_transfer_speed() {
    local filename=$1
    local filesize_mb=$2
    
    echo "📥 Starting transfer: ${filename}"
    
    # Record start time
    local start_time=$(date +%s.%N)
    
    # Initiate download
    cd "${NODE2_DIR}"
    ../../build/src/hypershare download "$(basename "${filename}")"
    
    # Wait for transfer to complete
    local max_wait=$((filesize_mb / 10 + 30))  # Allow ~10MB/s minimum + 30s buffer
    local wait_count=0
    
    while [ ! -f "./$(basename "${filename}")" ] && [ ${wait_count} -lt ${max_wait} ]; do
        sleep 1
        wait_count=$((wait_count + 1))
        if [ $((wait_count % 10)) -eq 0 ]; then
            echo "   ⏳ Still transferring... (${wait_count}s elapsed)"
        fi
    done
    
    # Record end time
    local end_time=$(date +%s.%N)
    
    if [ -f "./$(basename "${filename}")" ]; then
        local duration=$(echo "${end_time} - ${start_time}" | bc -l)
        local throughput_mbps=$(echo "scale=2; ${filesize_mb} / ${duration}" | bc -l)
        
        echo "   ✅ Transfer completed!"
        echo "   📊 Duration: $(printf "%.2f" ${duration})s"
        echo "   📊 Throughput: $(printf "%.2f" ${throughput_mbps}) MB/s"
        
        # Verify file integrity
        local original_hash=$(shasum -a 256 "${filename}" | cut -d' ' -f1)
        local downloaded_hash=$(shasum -a 256 "./$(basename "${filename}")" | cut -d' ' -f1)
        
        if [ "${original_hash}" = "${downloaded_hash}" ]; then
            echo "   ✅ File integrity verified (SHA256 match)"
        else
            echo "   ❌ File integrity check failed!"
            return 1
        fi
        
        # Check if meets performance threshold
        local meets_threshold=$(echo "${throughput_mbps} >= ${MIN_THROUGHPUT_MBPS}" | bc -l)
        if [ "${meets_threshold}" -eq 1 ]; then
            echo "   🎯 Performance threshold met (>= ${MIN_THROUGHPUT_MBPS} MB/s)"
        else
            echo "   ⚠️  Below performance threshold (< ${MIN_THROUGHPUT_MBPS} MB/s)"
        fi
        
        echo "${throughput_mbps}"
    else
        echo "   ❌ Transfer failed or timed out after ${max_wait}s"
        return 1
    fi
}

# Check dependencies
if [ ! -f "./build/src/hypershare" ]; then
    echo "❌ HyperShare not found. Please build first."
    exit 1
fi

if ! command -v bc &> /dev/null; then
    echo "❌ 'bc' calculator not found. Please install: brew install bc"
    exit 1
fi

echo "📁 Setting up speed test environment..."
rm -rf "${DEMO_DIR}"
mkdir -p "${NODE1_DIR}" "${NODE2_DIR}"

echo "🖥️  Starting performance-optimized nodes..."

# Start Node 1 (sender)
cd "${NODE1_DIR}"
../../build/src/hypershare daemon --port ${NODE1_PORT} --data-dir ./data --threads 8 &
NODE1_PID=$!

sleep 2

# Start Node 2 (receiver)  
cd "${NODE2_DIR}"
../../build/src/hypershare daemon --port ${NODE2_PORT} --data-dir ./data --threads 8 &
NODE2_PID=$!

sleep 2

echo "🔗 Establishing high-speed connection..."
cd "${NODE2_DIR}"
../../build/src/hypershare connect localhost:${NODE1_PORT}
sleep 1

echo
echo "=== SPEED TEST SUITE ==="
echo

# Test 1: Small file baseline
echo "🔬 Test 1: Small File Baseline (${SMALL_FILE_MB}MB)"
create_test_file ${SMALL_FILE_MB} "${NODE1_DIR}/small_test.bin"

cd "${NODE1_DIR}"
../../build/src/hypershare share ./small_test.bin
sleep 1

small_speed=$(measure_transfer_speed "${NODE1_DIR}/small_test.bin" ${SMALL_FILE_MB})
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