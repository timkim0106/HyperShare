# HyperShare Demos & Examples

This directory contains comprehensive demonstrations of HyperShare's capabilities, from basic file sharing to advanced P2P network scenarios.

## Quick Start Demo
- **`quick_start.sh`** - 5-minute setup and file sharing between two machines
- **`quick_start_local.sh`** - Single-machine demo using different ports

## Performance Demonstrations
- **`speed_test/`** - 100MB/s throughput validation with large files
- **`concurrent_transfers/`** - Multiple simultaneous file transfers
- **`network_stress/`** - High-load scenarios with many peers

## Real-World Scenarios
- **`development_team/`** - Code sharing workflow for development teams
- **`media_distribution/`** - Large media file distribution network
- **`backup_sync/`** - Distributed backup and synchronization

## Advanced Features
- **`crypto_showcase/`** - Secure transfer with encryption demonstrations
- **`mesh_network/`** - Multi-hop P2P routing through intermediate peers
- **`monitoring_dashboard/`** - Real-time transfer monitoring and statistics

## Performance Benchmarks
- **`benchmarks/`** - Automated performance testing and reporting

Each demo includes:
- 📋 Step-by-step setup instructions
- 🚀 Automated scripts for quick execution
- 📊 Expected performance metrics
- 🔧 Troubleshooting guides
- 📹 Sample outputs and logs

## Prerequisites
- HyperShare built and installed
- Network connectivity between demo machines
- Sufficient disk space for test files (some demos use GB-sized files)

## Running Demos
```bash
# Quick local demo
cd demos && ./quick_start_local.sh

# Network performance test
cd demos/speed_test && ./run_speed_test.sh

# Multi-machine setup
cd demos/development_team && ./setup_team_network.sh
```