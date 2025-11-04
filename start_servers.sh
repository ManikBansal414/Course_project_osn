#!/bin/bash

# Start servers in background with proper delays

echo "Starting Distributed File System..."
echo ""

# Clean up any existing processes
pkill -9 name_server 2>/dev/null
pkill -9 storage_server 2>/dev/null
sleep 1

# Create necessary directories
mkdir -p storage undo

echo "Step 1: Starting Name Server..."
./name_server > /dev/null 2>&1 &
NM_PID=$!
echo "  Name Server PID: $NM_PID"
sleep 2

echo ""
echo "Step 2: Starting Storage Server..."
./storage_server > /dev/null 2>&1 &
SS_PID=$!
echo "  Storage Server PID: $SS_PID"
sleep 3

echo ""
echo "✅ System is ready!"
echo ""
echo "Processes running:"
echo "  Name Server: PID $NM_PID"
echo "  Storage Server: PID $SS_PID"
echo ""
echo "Now you can run clients:"
echo "  ./client"
echo ""
echo "To stop the servers:"
echo "  kill $NM_PID $SS_PID"
echo "  or run: pkill name_server && pkill storage_server"
echo ""
