# HyperShare

A high-performance P2P file sharing system built with C++20.

## What is this?

I wanted to build something that could move files between machines quickly and securely, without relying on cloud services. HyperShare uses modern C++20 and cryptography to create direct peer-to-peer connections for file transfers.

## What works right now

- Daemon + IPC design: Single background process with multiple CLI clients  
- Secure file transfers: Ed25519 authentication, ChaCha20 encryption, Blake3 integrity
- Real-time status: Live monitoring of transfers and peer connections
- File management: Chunked uploads with resume support
- Network discovery: UDP multicast for automatic peer finding
- Solid testing: Comprehensive test suite with 799+ assertions

## Quick Start

```bash
# Build the project
mkdir build && cd build
cmake .. && make

# Start the daemon
./build/src/hypershare start

# Share a file
./build/src/hypershare share document.pdf

# Connect to another machine  
./build/src/hypershare connect 192.168.1.100:8080

# Check what's happening
./build/src/hypershare status
```

## Why C++20?

This project showcases modern C++ features:
- Coroutines for async networking
- Concepts for type safety
- Ranges for clean data processing
- Modules (where supported)
- More C++ practice!

## Performance Goals

- 100+ MB/s on local networks
- Sub-millisecond peer discovery
- Support for 100+ concurrent connections
- Minimal memory footprint

## Architecture

The system is built in layers:
- Network layer handles TCP/UDP communication
- Crypto layer provides encryption and authentication  
- Storage layer manages file chunks and metadata
- P2P layer coordinates peer discovery and routing

## Building

Requires C++20 compiler and vcpkg for dependencies.

```bash
git clone https://github.com/timkim0106/HyperShare.git
cd HyperShare
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake ..
make -j$(nproc)
```

## Status

The core system works - you can share files between machines and it handles the crypto properly. Still working on multi-hop and P2P routing layer, a web dashboard for monitoring, and performance validation.

## Try the demos

Check out `demos/` for different scenarios:
- `quick_start_local_fixed.sh` - Basic demo with one daemon
- `terminal_screenshot_demo.sh` - Perfect setup for screenshots

## License

MIT
