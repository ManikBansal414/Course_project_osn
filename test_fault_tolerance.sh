#!/bin/bash

echo "=========================================="
echo "Fault Tolerance Testing Script"
echo "=========================================="
echo ""
echo "This script will test:"
echo "1. Heartbeat mechanism (SS → NM)"
echo "2. Failure detection (NM monitors SS)"
echo "3. Automatic failover (replica promotion)"
echo ""

# Clean up previous test
echo "Cleaning up previous test..."
pkill -9 name_server 2>/dev/null
pkill -9 storage_server 2>/dev/null
pkill -9 client 2>/dev/null
rm -f nm_log.txt ss_log.txt nm_metadata.dat
rm -rf storage undo
sleep 1

# Start Name Server
echo "Starting Name Server..."
./name_server &
NM_PID=$!
sleep 2

# Start primary Storage Server on port 8001
echo "Starting Storage Server 1 (Primary) on port 8001..."
./storage_server 8001 9001 &
SS1_PID=$!
sleep 2

# Start replica Storage Server on port 8002
echo "Starting Storage Server 2 (Replica) on port 8002..."
./storage_server 8002 9002 &
SS2_PID=$!
sleep 2

echo ""
echo "✓ All servers started"
echo "  - Name Server PID: $NM_PID"
echo "  - Storage Server 1 PID: $SS1_PID"
echo "  - Storage Server 2 PID: $SS2_PID"
echo ""

# Wait for heartbeats to be sent
echo "Waiting 15 seconds for heartbeat messages..."
sleep 15

echo ""
echo "=========================================="
echo "Checking Name Server logs for heartbeats"
echo "=========================================="
if [ -f nm_log.txt ]; then
    echo "Heartbeat messages received:"
    grep -i "heartbeat" nm_log.txt | tail -5
    echo ""
    echo "Storage Server status:"
    grep -i "storage server.*active" nm_log.txt | tail -5
else
    echo "WARNING: nm_log.txt not found"
fi

echo ""
echo "=========================================="
echo "Checking Storage Server logs"
echo "=========================================="
if [ -f ss_log.txt ]; then
    echo "Heartbeat messages sent:"
    grep -i "heartbeat" ss_log.txt | tail -10
else
    echo "WARNING: ss_log.txt not found"
fi

echo ""
echo "=========================================="
echo "Simulating Storage Server 1 failure"
echo "=========================================="
echo "Killing Storage Server 1 (PID: $SS1_PID)..."
kill -9 $SS1_PID
echo "✓ Storage Server 1 killed"

echo ""
echo "Waiting 35 seconds for Name Server to detect failure..."
echo "(Name Server checks every 5 seconds, timeout is 30 seconds)"
sleep 35

echo ""
echo "=========================================="
echo "Checking failure detection in NM logs"
echo "=========================================="
if [ -f nm_log.txt ]; then
    echo "Failure detection messages:"
    grep -i "inactive\|failover" nm_log.txt | tail -10
else
    echo "WARNING: nm_log.txt not found"
fi

echo ""
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo ""
echo "Expected behavior:"
echo "1. ✓ Storage Servers should send heartbeats every 10 seconds"
echo "2. ✓ Name Server should receive and log heartbeats"
echo "3. ✓ After killing SS1, NM should mark it INACTIVE after 30s"
echo "4. ✓ NM should trigger failover for files on SS1 (if replicas exist)"
echo ""
echo "Check the logs above to verify this behavior."
echo ""

# Cleanup
echo "Cleaning up..."
kill $NM_PID 2>/dev/null
kill $SS2_PID 2>/dev/null
sleep 1
pkill -9 name_server 2>/dev/null
pkill -9 storage_server 2>/dev/null

echo "✓ Test complete"
echo ""
echo "Log files available for review:"
echo "  - nm_log.txt (Name Server logs)"
echo "  - ss_log.txt (Storage Server logs)"
