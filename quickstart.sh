#!/bin/bash

# Quick Start Script for Distributed File System

echo "═══════════════════════════════════════════════════════════"
echo "   Distributed File System - Quick Start Guide"
echo "═══════════════════════════════════════════════════════════"
echo ""

# Check if files exist
if [ ! -f "common.c" ] || [ ! -f "name_server.c" ] || [ ! -f "storage_server.c" ] || [ ! -f "client.c" ]; then
    echo "ERROR: Source files not found in current directory"
    exit 1
fi

echo "Step 1: Building the project..."
make clean > /dev/null 2>&1
make

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo ""
echo "✓ Build successful!"
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "How to run the system:"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "1. Start Name Server (in terminal 1):"
echo "   $ ./name_server"
echo ""
echo "2. Start Storage Server (in terminal 2):"
echo "   $ ./storage_server"
echo "   Or with custom ports:"
echo "   $ ./storage_server 9000 9001"
echo ""
echo "3. Start Client (in terminal 3+):"
echo "   $ ./client"
echo "   Enter a username when prompted"
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "Example Commands:"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "  CREATE test.txt                    # Create a file"
echo "  WRITE test.txt 0                   # Write to file"
echo "    1 Hello world.                   # Add content"
echo "    ETIRW                             # Finish writing"
echo "  READ test.txt                       # Read file"
echo "  VIEW -l                             # List files with details"
echo "  INFO test.txt                       # Get file info"
echo "  STREAM test.txt                     # Stream content"
echo "  ADDACCESS -R test.txt user2        # Grant read access"
echo "  LIST                                # List all users"
echo "  UNDO test.txt                       # Undo last change"
echo "  DELETE test.txt                     # Delete file"
echo ""
echo "Type HELP in the client for full command list"
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "Log Files:"
echo "═══════════════════════════════════════════════════════════"
echo "  nm_log.txt     - Name Server operations log"
echo "  ss_log.txt     - Storage Server operations log"
echo ""
echo "Ready to start! Open multiple terminals and follow the steps above."
echo ""
