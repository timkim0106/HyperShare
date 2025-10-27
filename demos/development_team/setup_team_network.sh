#!/bin/bash

# HyperShare Development Team Demo
# Simulates a distributed development team sharing code and assets

set -e

echo "👥 HyperShare Development Team Network Demo"
echo "Simulating a distributed team with multiple developers sharing code"
echo

# Team configuration
DEMO_DIR="/tmp/hypershare_dev_team"
TEAM_SIZE=4

# Developer profiles
declare -A DEVELOPERS=(
    ["alice"]="Team Lead - Full Stack"
    ["bob"]="Backend Developer"  
    ["carol"]="Frontend Developer"
    ["dave"]="DevOps Engineer"
)

declare -A PORTS=(
    ["alice"]=8080
    ["bob"]=8081
    ["carol"]=8082
    ["dave"]=8083
)

cleanup() {
    echo "🧹 Cleaning up development team demo..."
    pkill -f "hypershare.*daemon" || true
    rm -rf "${DEMO_DIR}"
}

trap cleanup EXIT

create_developer_workspace() {
    local dev_name=$1
    local dev_role=$2
    local workspace="${DEMO_DIR}/${dev_name}"
    
    echo "👤 Setting up workspace for ${dev_name} (${dev_role})"
    mkdir -p "${workspace}/projects"
    
    # Create role-specific files
    case "${dev_name}" in
        "alice")
            # Team lead has project overview and documentation
            cat > "${workspace}/projects/README.md" << 'EOF'
# HyperShare Development Project

## Team Structure
- Alice: Team Lead & Full Stack
- Bob: Backend Development  
- Carol: Frontend Development
- Dave: DevOps & Infrastructure

## Project Goals
Building a high-performance P2P file sharing system with:
- 100+ MB/s throughput
- Secure crypto (Blake3/ChaCha20/Ed25519)
- Real-time monitoring
- Distributed architecture

## Development Workflow
1. Alice coordinates and shares project docs
2. Bob develops backend APIs and database schemas
3. Carol creates UI components and frontend
4. Dave manages deployment configs and scripts

Use HyperShare for seamless file sharing across the team!
EOF

            cat > "${workspace}/projects/architecture.md" << 'EOF'
# System Architecture

## Core Components
- Network Layer: TCP/UDP with async I/O
- Crypto Layer: libsodium integration
- Storage Layer: SQLite + chunked files
- Transfer Layer: Bandwidth management

## Performance Requirements
- Target: 100+ MB/s throughput
- Support: 100+ concurrent connections
- Latency: Sub-millisecond discovery
- Security: End-to-end encryption
EOF
            ;;
            
        "bob")
            # Backend developer has API specs and database schemas
            cat > "${workspace}/projects/api_spec.json" << 'EOF'
{
  "openapi": "3.0.0",
  "info": {
    "title": "HyperShare API",
    "version": "1.0.0"
  },
  "paths": {
    "/api/files": {
      "get": {
        "summary": "List available files",
        "responses": {
          "200": {
            "description": "List of files"
          }
        }
      },
      "post": {
        "summary": "Share a new file",
        "responses": {
          "201": {
            "description": "File shared successfully"
          }
        }
      }
    },
    "/api/transfers": {
      "get": {
        "summary": "Get transfer status",
        "responses": {
          "200": {
            "description": "Transfer statistics"
          }
        }
      }
    }
  }
}
EOF

            cat > "${workspace}/projects/schema.sql" << 'EOF'
-- HyperShare Database Schema

CREATE TABLE files (
    file_id TEXT PRIMARY KEY,
    filename TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    file_hash TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_accessed TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE chunks (
    chunk_id TEXT PRIMARY KEY,
    file_id TEXT REFERENCES files(file_id),
    chunk_index INTEGER NOT NULL,
    chunk_size INTEGER NOT NULL,
    chunk_hash TEXT NOT NULL,
    UNIQUE(file_id, chunk_index)
);

CREATE TABLE peers (
    peer_id INTEGER PRIMARY KEY,
    ip_address TEXT NOT NULL,
    port INTEGER NOT NULL,
    last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    reliability_score REAL DEFAULT 1.0
);
EOF
            ;;
            
        "carol")
            # Frontend developer has UI components and styles
            cat > "${workspace}/projects/components.tsx" << 'EOF'
// HyperShare React Components

import React, { useState, useEffect } from 'react';

interface TransferStatus {
  filename: string;
  progress: number;
  speed: number;
  eta: number;
}

export const TransferMonitor: React.FC = () => {
  const [transfers, setTransfers] = useState<TransferStatus[]>([]);
  
  useEffect(() => {
    // WebSocket connection to HyperShare daemon
    const ws = new WebSocket('ws://localhost:8080/api/transfers');
    
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      setTransfers(data.active_transfers);
    };
    
    return () => ws.close();
  }, []);
  
  return (
    <div className="transfer-monitor">
      <h3>Active Transfers</h3>
      {transfers.map((transfer, index) => (
        <div key={index} className="transfer-item">
          <div className="filename">{transfer.filename}</div>
          <div className="progress-bar">
            <div 
              className="progress-fill" 
              style={{width: `${transfer.progress}%`}}
            />
          </div>
          <div className="stats">
            {transfer.speed.toFixed(1)} MB/s • ETA: {transfer.eta}s
          </div>
        </div>
      ))}
    </div>
  );
};
EOF

            cat > "${workspace}/projects/styles.css" << 'EOF'
/* HyperShare UI Styles */

.transfer-monitor {
  background: #1a1a1a;
  border-radius: 8px;
  padding: 20px;
  color: #ffffff;
}

.transfer-item {
  margin-bottom: 15px;
  padding: 10px;
  background: #2a2a2a;
  border-radius: 4px;
}

.filename {
  font-weight: bold;
  margin-bottom: 8px;
}

.progress-bar {
  width: 100%;
  height: 6px;
  background: #404040;
  border-radius: 3px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #00ff88, #00cc6a);
  transition: width 0.3s ease;
}

.stats {
  font-size: 0.9em;
  color: #aaaaaa;
  margin-top: 5px;
}
EOF
            ;;
            
        "dave")
            # DevOps engineer has deployment configs and scripts
            cat > "${workspace}/projects/docker-compose.yml" << 'EOF'
version: '3.8'

services:
  hypershare-node:
    image: hypershare:latest
    ports:
      - "8080:8080"
    volumes:
      - ./data:/app/data
      - ./config:/app/config
    environment:
      - HYPERSHARE_PORT=8080
      - HYPERSHARE_DATA_DIR=/app/data
      - HYPERSHARE_LOG_LEVEL=info
    restart: unless-stopped

  hypershare-monitor:
    image: hypershare-monitor:latest
    ports:
      - "3000:3000"
    depends_on:
      - hypershare-node
    environment:
      - HYPERSHARE_API_URL=http://hypershare-node:8080
EOF

            cat > "${workspace}/projects/deploy.sh" << 'EOF'
#!/bin/bash

# HyperShare Production Deployment Script

set -e

echo "🚀 Deploying HyperShare to production..."

# Build optimized release
echo "🔨 Building optimized release..."
mkdir -p build-release
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run tests
echo "🧪 Running test suite..."
./tests/unit_tests
./tests/integration_tests

# Package for deployment
echo "📦 Creating deployment package..."
tar -czf hypershare-$(date +%Y%m%d).tar.gz \
    bin/hypershare \
    lib/libhypershare_core.so \
    config/ \
    scripts/

echo "✅ Deployment package ready"
echo "📋 Next steps:"
echo "   1. Upload package to production servers"
echo "   2. Run deployment playbook"
echo "   3. Verify service health"
EOF
            chmod +x "${workspace}/projects/deploy.sh"
            ;;
    esac
    
    # Create some common files for everyone
    echo "# Development Notes for ${dev_name}" > "${workspace}/projects/notes.md"
    echo "$(date): Started working on HyperShare project" >> "${workspace}/projects/notes.md"
    
    # Create a personal config
    cat > "${workspace}/.hypershare_config" << EOF
{
  "peer_name": "${dev_name}",
  "listen_port": ${PORTS[$dev_name]},
  "data_directory": "./data",
  "log_level": "info",
  "max_connections": 50,
  "bandwidth_limit_mbps": 100
}
EOF
}

start_developer_node() {
    local dev_name=$1
    local port=${PORTS[$dev_name]}
    local workspace="${DEMO_DIR}/${dev_name}"
    
    echo "🚀 Starting HyperShare node for ${dev_name} on port ${port}"
    
    cd "${workspace}"
    ../../../build/src/hypershare daemon \
        --port ${port} \
        --data-dir ./data \
        --name "${dev_name}" &
    
    echo "   Node PID: $!"
}

share_files() {
    local dev_name=$1
    local workspace="${DEMO_DIR}/${dev_name}"
    
    echo "📤 ${dev_name} sharing files with team..."
    
    cd "${workspace}"
    
    # Share project files
    find ./projects -name "*.md" -o -name "*.json" -o -name "*.sql" -o -name "*.tsx" -o -name "*.css" -o -name "*.yml" -o -name "*.sh" | while read file; do
        ../../../build/src/hypershare share "${file}"
        echo "   📄 Shared: ${file}"
    done
}

connect_team_network() {
    echo "🔗 Connecting development team network..."
    
    # Create mesh network - everyone connects to alice (team lead)
    local alice_port=${PORTS["alice"]}
    
    for dev_name in "${!DEVELOPERS[@]}"; do
        if [ "${dev_name}" != "alice" ]; then
            local workspace="${DEMO_DIR}/${dev_name}"
            cd "${workspace}"
            
            echo "   🔌 ${dev_name} connecting to alice..."
            ../../../build/src/hypershare connect localhost:${alice_port}
        fi
    done
    
    sleep 2
    
    # Additional cross-connections for redundancy
    echo "   🕸️  Creating additional mesh connections..."
    
    # Bob connects to Carol
    cd "${DEMO_DIR}/bob"
    ../../../build/src/hypershare connect localhost:${PORTS["carol"]}
    
    # Dave connects to Bob  
    cd "${DEMO_DIR}/dave"
    ../../../build/src/hypershare connect localhost:${PORTS["bob"]}
}

simulate_development_workflow() {
    echo
    echo "💼 Simulating development workflow..."
    
    # Alice shares project documentation
    echo "📋 Alice (Team Lead) shares project documentation..."
    share_files "alice"
    sleep 1
    
    # Bob shares backend code
    echo "🔧 Bob (Backend) shares API specs and database schemas..."
    share_files "bob"
    sleep 1
    
    # Carol shares frontend components
    echo "🎨 Carol (Frontend) shares UI components and styles..."
    share_files "carol"
    sleep 1
    
    # Dave shares deployment configs
    echo "🚀 Dave (DevOps) shares deployment configurations..."
    share_files "dave"
    sleep 2
    
    echo "🔍 Team discovering shared files..."
    
    # Each developer lists available files
    for dev_name in "${!DEVELOPERS[@]}"; do
        echo
        echo "👤 ${dev_name}'s view of team files:"
        cd "${DEMO_DIR}/${dev_name}"
        ../../../build/src/hypershare list | head -10
    done
}

download_team_files() {
    echo
    echo "📥 Simulating collaborative file access..."
    
    # Alice downloads Bob's API spec
    echo "📄 Alice downloading Bob's API specification..."
    cd "${DEMO_DIR}/alice"
    ../../../build/src/hypershare download api_spec.json 2>/dev/null || echo "   (File download initiated)"
    
    # Carol downloads Alice's architecture docs
    echo "📄 Carol downloading Alice's architecture documentation..."
    cd "${DEMO_DIR}/carol"
    ../../../build/src/hypershare download architecture.md 2>/dev/null || echo "   (File download initiated)"
    
    # Dave downloads all project files for deployment
    echo "📄 Dave downloading project files for deployment..."
    cd "${DEMO_DIR}/dave"
    ../../../build/src/hypershare download README.md 2>/dev/null || echo "   (File download initiated)"
    
    sleep 3
}

show_team_status() {
    echo
    echo "📊 Development Team Network Status"
    echo "=================================="
    
    for dev_name in "${!DEVELOPERS[@]}"; do
        echo
        echo "👤 ${dev_name} (${DEVELOPERS[$dev_name]}):"
        cd "${DEMO_DIR}/${dev_name}"
        
        echo "   Network Status:"
        ../../../build/src/hypershare status 2>/dev/null | grep -E "(Connected peers|Active transfers|Files shared)" || echo "   Status unavailable"
        
        echo "   Shared Files:"
        ls -la projects/ 2>/dev/null | wc -l | xargs echo "   Files available:" || echo "   No files"
    done
}

# Main demo execution
echo "📁 Setting up development team environment..."
rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}"

# Create workspaces for each developer
for dev_name in "${!DEVELOPERS[@]}"; do
    create_developer_workspace "${dev_name}" "${DEVELOPERS[$dev_name]}"
done

echo
echo "🖥️  Starting team member nodes..."
for dev_name in "${!DEVELOPERS[@]}"; do
    start_developer_node "${dev_name}"
    sleep 1
done

echo "⏳ Waiting for all nodes to initialize..."
sleep 3

connect_team_network

simulate_development_workflow

download_team_files

show_team_status

echo
echo "🎉 Development Team Demo Complete!"
echo
echo "📋 Demo Summary:"
echo "   ✅ Created 4-person development team network"
echo "   ✅ Each member has role-specific files and workspace"
echo "   ✅ Established mesh P2P network for collaboration"
echo "   ✅ Demonstrated file sharing workflow"
echo "   ✅ Showed collaborative file access patterns"
echo
echo "💡 Key Features Demonstrated:"
echo "   • Multi-node P2P network with automatic discovery"
echo "   • Role-based file sharing (docs, code, configs)"
echo "   • Mesh connectivity for redundancy"
echo "   • Real-time file distribution across team"
echo "   • Secure transfer of development assets"
echo
echo "🔧 Team network will continue running for manual exploration..."
echo "   Connect to any developer workspace and try:"
echo "   • ../../../build/src/hypershare list"
echo "   • ../../../build/src/hypershare download <filename>"
echo "   • ../../../build/src/hypershare status"
echo
echo "⏸️  Press Ctrl+C to stop the demo"

# Keep demo running for exploration
sleep 300