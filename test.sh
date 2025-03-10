#!/bin/bash

# Constants
SERVER_HOST="localhost"
SERVER_PORT="41418"
TEST_DIR="test_files"
OUTPUT_DIR="output_files"
LOG_DIR="logs"

# Test results tracking
declare -A test_results
test_count=0
pass_count=0

# Ensure required directories exist
mkdir -p $TEST_DIR $OUTPUT_DIR $LOG_DIR

# Create test files of different sizes if they don't exist
if [ ! -f "$TEST_DIR/small.dat" ]; then
    dd if=/dev/urandom of="$TEST_DIR/small.dat" bs=1 count=800 2>/dev/null
    echo "Created small test file (800 bytes)"
fi

if [ ! -f "$TEST_DIR/medium.dat" ]; then
    dd if=/dev/urandom of="$TEST_DIR/medium.dat" bs=1024 count=50 2>/dev/null
    echo "Created medium test file (~50KB)"
fi

if [ ! -f "$TEST_DIR/large.dat" ]; then
    dd if=/dev/urandom of="$TEST_DIR/large.dat" bs=1024 count=420 2>/dev/null
    echo "Created large test file (~420KB)"
fi

# Function to start the server
start_server() {
    local error_rate=$1
    
    # Kill any existing server process
    pkill -f "server $error_rate $SERVER_PORT" 2>/dev/null
    
    # Start server in background
    echo "Starting server with error rate: $error_rate"
    ./server $error_rate $SERVER_PORT > "$LOG_DIR/server.log" 2>&1 &
    
    # Store server PID
    SERVER_PID=$!
    
    # Give the server time to start
    sleep 1
    
    # Verify server started correctly
    if ! ps -p $SERVER_PID > /dev/null; then
        echo "ERROR: Server failed to start"
        exit 1
    fi
    
    echo "Server started with PID: $SERVER_PID"
}

# Function to stop the server
stop_server() {
    if [ -n "$SERVER_PID" ]; then
        echo "Stopping server (PID: $SERVER_PID)"
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        SERVER_PID=""
    fi
}

# Function to record test result
record_test_result() {
    local test_name=$1
    local result=$2
    
    test_results["$test_name"]=$result
    test_count=$((test_count + 1))
    if [ "$result" = "PASS" ]; then
        pass_count=$((pass_count + 1))
    fi
}

# Function to run a single file transfer
run_copy() {
    local input_file=$1
    local output_file=$2
    local window_size=$3
    local buffer_size=$4
    local error_rate=$5
    local drop_packets=$6
    local test_name=$7
    
    if [ -z "$test_name" ]; then
        test_name="Copy $input_file (w=$window_size, b=$buffer_size, e=$error_rate)"
    fi
    
    echo "-----------------------------------------------------"
    echo "Test: $test_name"
    
    # Set packet drops if specified
    if [ -n "$drop_packets" ]; then
        echo "Setting packet drops: $drop_packets"
        export CPE464_OVERRIDE_ERR_DROP="$drop_packets"
    else
        unset CPE464_OVERRIDE_ERR_DROP
    fi
    
    # Run rcopy and capture output and time
    local log_file="$LOG_DIR/$(basename $input_file)_w${window_size}_b${buffer_size}_e${error_rate}_$(date +%s).log"
    echo "Running: ./rcopy $TEST_DIR/$input_file $OUTPUT_DIR/$output_file $window_size $buffer_size $error_rate $SERVER_HOST $SERVER_PORT"
    
    # Use time command to measure execution time
    { time ./rcopy $TEST_DIR/$input_file $OUTPUT_DIR/$output_file $window_size $buffer_size $error_rate $SERVER_HOST $SERVER_PORT; } 2>&1 | tee $log_file
    
    local test_result="PASS"
    
    # Check if files match (unless we're testing "file not found")
    if [[ "$input_file" != "nonexistent_file.dat" ]]; then
        echo "Verifying file integrity..."
        if cmp -s "$TEST_DIR/$input_file" "$OUTPUT_DIR/$output_file"; then
            echo "✅ SUCCESS: Files match"
        else
            echo "❌ FAILURE: Files do not match"
            test_result="FAIL"
            # Get file sizes for debugging
            original_size=$(wc -c < "$TEST_DIR/$input_file")
            output_size=$(wc -c < "$OUTPUT_DIR/$output_file" 2>/dev/null || echo "0")
            echo "Original size: $original_size bytes, Output size: $output_size bytes"
        fi
    else
        # For "file not found" test, success means rcopy reported error and exited properly
        grep -q "Error: file nonexistent_file.dat not found" $log_file
        if [ $? -eq 0 ]; then
            echo "✅ SUCCESS: File not found error properly reported"
        else
            echo "❌ FAILURE: File not found error not properly reported"
            test_result="FAIL"
        fi
    fi
    
    record_test_result "$test_name" "$test_result"
    echo "Log saved to: $log_file"
    echo ""
}

# Function to run multiple clients simultaneously
run_multiple_clients() {
    echo "========================================================"
    echo "TEST CASE: Multiple simultaneous clients"
    echo "========================================================"
    
    # Start with clean output directory
    rm -f $OUTPUT_DIR/*
    
    # Run clients in parallel
    run_copy "small.dat" "small_out1.dat" 10 1000 0.2 "" "Multiple clients - small file" &
    pid1=$!
    
    run_copy "medium.dat" "medium_out1.dat" 5 900 0.15 "" "Multiple clients - medium file" &
    pid2=$!
    
    run_copy "large.dat" "large_out1.dat" 20 1000 0.1 "" "Multiple clients - large file" &
    pid3=$!
    
    # Wait for all clients to finish
    wait $pid1 $pid2 $pid3
    
    echo "Multiple client test completed"
}

# Function to display test summary
display_summary() {
    echo ""
    echo "========================================================"
    echo "TEST SUMMARY"
    echo "========================================================"
    echo "Total tests: $test_count"
    echo "Passed: $pass_count"
    echo "Failed: $((test_count - pass_count))"
    echo ""
    echo "Detailed Results:"
    echo "-------------------------------------------------------"
    
    for test_name in "${!test_results[@]}"; do
        result=${test_results["$test_name"]}
        if [ "$result" = "PASS" ]; then
            echo "✅ PASS: $test_name"
        else
            echo "❌ FAIL: $test_name"
        fi
    done
    
    echo "========================================================"
    
    # Calculate overall pass percentage
    if [ $test_count -gt 0 ]; then
        pass_percentage=$((pass_count * 100 / test_count))
        echo "Overall pass rate: $pass_percentage%"
    fi
    
    echo "========================================================"
}

# Function to check for prohibited functions
check_prohibited_functions() {
    local test_name="Check for prohibited functions"
    local test_result="PASS"
    
    echo "Checking for sleep functions:"
    if grep -r "sleep" *.c *.h 2>/dev/null; then
        echo "❌ FAILURE: sleep functions found"
        test_result="FAIL"
    else
        echo "✅ SUCCESS: No sleep functions found"
    fi
    
    echo "Checking for seek functions:"
    if grep -r "seek" *.c *.h 2>/dev/null; then
        echo "❌ FAILURE: seek functions found"
        test_result="FAIL"
    else
        echo "✅ SUCCESS: No seek functions found"
    fi
    
    record_test_result "$test_name" "$test_result"
}

# Main test sequence
echo "========================================================"
echo "SELECTIVE REJECT FILE TRANSFER TEST SUITE"
echo "========================================================"
echo "Starting tests at $(date)"
echo ""

# Test 1: Basic tests from grading sheet
echo "========================================================"
echo "TEST CASE 1: Basic tests from grading sheet"
echo "========================================================"

# Start server with 0 error rate for packet dropping tests
start_server 0

# Test 1.1: Small file, window=10, buffer=1000, client/server error=0.2
run_copy "small.dat" "small_out.dat" 10 1000 0.2 "" "1.1: Small file, standard test"

# Test 1.2: Medium file, window=10, buffer=1000, client/server error=0.2
run_copy "medium.dat" "medium_out.dat" 10 1000 0.2 "" "1.2: Medium file, standard test"

# Test 1.3: Large file, window=50, buffer=1000, client/server error=0.1
run_copy "large.dat" "large_out1.dat" 50 1000 0.1 "" "1.3: Large file, large window"

# Test 1.4: Large file, window=5, buffer=1000, client/server error=0.15
run_copy "large.dat" "large_out2.dat" 5 1000 0.15 "" "1.4: Large file, small window"

# Stop and restart server for next tests
stop_server

# Test 2: Drop packets at EOF
echo "========================================================"
echo "TEST CASE 2: Drop packets near EOF"
echo "========================================================"

# Estimate packets for medium file (50KB with 1000B buffer = ~50 packets)
# Drop packets near the end (~40-45)
start_server 0
run_copy "medium.dat" "medium_eof_drop.dat" 10 1000 0 "40,41,42,43,44,45" "2: Drop packets near EOF"
stop_server

# Test 3: Drop an entire window of packets
echo "========================================================"
echo "TEST CASE 3: Drop an entire window"
echo "========================================================"

# Drop a full window (packets 20-29) for window size 10
start_server 0
run_copy "medium.dat" "medium_window_drop.dat" 10 1000 0 "20,21,22,23,24,25,26,27,28,29" "3: Drop entire window"
stop_server

# Test 4: Stop and wait test (window size = 1)
echo "========================================================"
echo "TEST CASE 4: Stop and wait (window=1)"
echo "========================================================"

start_server 0.2
run_copy "medium.dat" "medium_stop_wait.dat" 1 1000 0.2 "" "4: Stop-and-wait (window=1)"
stop_server

# Test 5: Multiple clients test
echo "========================================================"
echo "TEST CASE 5: Multiple clients simultaneously"
echo "========================================================"

start_server 0.1
run_multiple_clients
stop_server

# Test 6: Drop specific packets as per grading sheet
echo "========================================================"
echo "TEST CASE 6: Specific packet drops (grading sheet test 6 & 7)"
echo "========================================================"

# Test 6.1: Drop packets 20-30 on server side
start_server 0
export CPE464_OVERRIDE_ERR_DROP="20,21,22,23,24,25,26,27,28,29,30"
run_copy "medium.dat" "medium_drop_server.dat" 10 1000 0 "" "6.1: Drop packets 20-30 (grading test 6)"

# Test 6.2: Drop specific packets (15,18,30,31,35,37)
unset CPE464_OVERRIDE_ERR_DROP
stop_server
start_server 0
run_copy "medium.dat" "medium_drop_client.dat" 10 1000 0 "15,18,30,31,35,37" "6.2: Drop packets 15,18,30,31,35,37 (grading test 7)"
stop_server

# Test 7: Error detection (bit flips)
echo "========================================================"
echo "TEST CASE 7: Error detection (bit flips)"
echo "========================================================"

# For bit flips we use error rate instead of specific packet drops
start_server 0.15
run_copy "medium.dat" "medium_bit_flips.dat" 10 1000 0.15 "" "7: Error detection (bit flips)"
stop_server

# Test 8: Very large window with small file
echo "========================================================"
echo "TEST CASE 8: Very large window with small file"
echo "========================================================"

start_server 0.1
run_copy "small.dat" "small_large_window.dat" 100 100 0.1 "" "8: Large window with small file"
stop_server

# Test 9: File not found test
echo "========================================================"
echo "TEST CASE 9: File not found"
echo "========================================================"

start_server 0
run_copy "nonexistent_file.dat" "should_not_exist.dat" 10 1000 0 "" "9: File not found error handling"
stop_server

# Test 10: Check for any sleep/seek functions
echo "========================================================"
echo "TEST CASE 10: Check for prohibited functions"
echo "========================================================"

check_prohibited_functions

Display test summary
display_summary

echo "All tests completed at $(date)"
echo "========================================================"