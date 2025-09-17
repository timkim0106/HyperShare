# HyperShare

A high-performance P2P file sharing system built with C++20.

## What is this?

I got tired of slow file transfers and complicated sharing setups, so I built my own. HyperShare uses C++20 features and modern cryptography to create a fast, secure way to share files directly between computers.

## Features

- Direct peer-to-peer transfers (no central server needed)
- Fast transfers using chunked delivery and parallel connections
- Strong encryption with ChaCha20-Poly1305 and forward secrecy
- Web interface for monitoring transfers
- Cross-platform support (Linux, macOS, Windows)

## Quick Start

```bash
# Build
mkdir build && cd build
cmake ..
make

# Start sharing
./hypershare start

# Share a file
./hypershare share document.pdf

# Connect to someone else
./hypershare connect 192.168.1.100
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

Currently in development. Core networking and crypto components are functional, working on P2P discovery and web interface.

## License

MIT
