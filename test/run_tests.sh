#!/bin/bash

###############################################################################
# Test harness for validating a C++ executable implementing an order book.
# Script location: project_root/test/run_tests_2.sh
#
# Execution from project root:
#       ./test/run_tests_2.sh --mode udp --bin ./build/matching_engine
#
# The harness drives the C++ engine by:
#   - Sending input from test/<N>/in.csv
#   - Capturing stdout produced by the engine
#   - Comparing results against test/<N>/out.csv
#
# Supports:
#   - UDP mode (engine listens on UDP port 1234)
#   - STDIN mode (engine reads from standard input)
#
# Ensures deterministic output capture, per‑test isolation, and consistent
# behavior across repeated runs.
###############################################################################

# Absolute path to the directory containing this script (test/)
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"

# Project root is the parent directory of test/
PROJECT_ROOT="$(cd "$TEST_DIR/.." && pwd)"

# Default binary path (can be overridden)
BIN="$PROJECT_ROOT/build/matching_engine"

# Colors
RED="\033[31m"
GREEN="\033[32m"
YELLOW="\033[33m"
BLUE="\033[34m"
RESET="\033[0m"

###############################################################################
# HARD RESET OF ANY STALE ENGINE OR FIFO READER BETWEEN RUNS
###############################################################################
pkill -9 -f matching_engine 2>/dev/null || true
pkill -9 -f "cat /tmp/output_fifo" 2>/dev/null || true

rm -f /tmp/output_fifo
rm -f /tmp/all_output.txt
rm -f /tmp/stderr.txt

mkfifo /tmp/output_fifo

###############################################################################
# AUTO-DETECT ENGINE MODE
###############################################################################
detect_mode() {
    "$BIN" >/dev/null 2>&1 &
    PID=$!
    sleep 0.2

    if lsof -p "$PID" 2>/dev/null | grep -q "UDP"; then
        kill "$PID" 2>/dev/null
        echo "udp"
        return
    fi

    kill "$PID" 2>/dev/null
    echo "stdin"
}

###############################################################################
# PORTABLE TIMEOUT (used for long‑running UDP engines)
###############################################################################
run_with_timeout() {
    local timeout="$1"
    shift
    local cmd=("$@")

    "${cmd[@]}" &
    local pid=$!

    (
        sleep "$timeout"
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
        fi
    ) &

    local watcher=$!
    wait "$pid"
    local exit_code=$?
    kill "$watcher" 2>/dev/null
    return $exit_code
}

###############################################################################
# PARSE ARGUMENTS
###############################################################################
USER_MODE=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --mode) USER_MODE="$2"; shift 2 ;;
        --bin)  BIN="$2"; shift 2 ;;
        *) shift ;;
    esac
done

# Convert BIN to absolute path
if [[ "$BIN" != /* ]]; then
    BIN="$PROJECT_ROOT/$BIN"
fi

# Auto-detect if not specified
if [ -z "$USER_MODE" ]; then
    ENGINE_MODE=$(detect_mode)
else
    ENGINE_MODE="$USER_MODE"
fi

echo ""
echo -e "${BLUE}🚀 Running Test Suite${RESET}"
echo "════════════════════════════════════════════════"
echo -e "   Mode: ${YELLOW}$ENGINE_MODE${RESET}"
echo "════════════════════════════════════════════════"
echo ""

failed_tests=0
passed_tests=0
declare -a timing_rows

start_time=$(date +%s)

###############################################################################
# UDP MODE SETUP
###############################################################################
if [ "$ENGINE_MODE" = "udp" ]; then

    rm -f /tmp/output_fifo
    mkfifo /tmp/output_fifo

    rm -f /tmp/all_output.txt
    touch /tmp/all_output.txt

    # FIFO reader
    cat /tmp/output_fifo >> /tmp/all_output.txt &
    CAT_PID=$!

    # Engine process
    run_with_timeout 9999 "$BIN" > /tmp/output_fifo 2>/tmp/stderr.txt &
    BIN_PID=$!

    sleep 0.3
fi

###############################################################################
# TEST LOOP
###############################################################################
for testcase in "$TEST_DIR"/[0-9]*; do
    [ -d "$testcase" ] || continue
    dirname=$(basename "$testcase")

    echo -e "${BLUE}Test Case: $dirname${RESET}"

    test_start=$(date +%s.%N)

    if [ "$ENGINE_MODE" = "stdin" ]; then

        "$BIN" \
            < "$TEST_DIR/$dirname/in.csv" \
            > "$TEST_DIR/test_output.csv" \
            2>/tmp/stderr.txt

        exit_code=$?

    else
        # UDP MODE

        expected_lines=$(wc -l < "$TEST_DIR/$dirname/out.csv")
        before_lines=$(wc -l < /tmp/all_output.txt)

        # Send via UDP
        while IFS= read -r line || [ -n "$line" ]; do
            printf "%s" "$line" > /dev/udp/127.0.0.1/1234
        done < "$TEST_DIR/$dirname/in.csv"

        # Wait for output
        timeout_count=0
        while [ $timeout_count -lt 50 ]; do
            current_lines=$(wc -l < /tmp/all_output.txt)
            received=$((current_lines - before_lines))
            if [ $received -ge $expected_lines ]; then
                break
            fi
            sleep 0.05
            timeout_count=$((timeout_count + 1))
        done

        # Extract new lines
        tail -n +$((before_lines + 1)) /tmp/all_output.txt \
            | head -n $expected_lines \
            > "$TEST_DIR/test_output.csv"

        exit_code=0
    fi

    test_end=$(date +%s.%N)
    test_time=$(echo "$test_end - $test_start" | bc)

    # Diff
    diff_output=$(diff "$TEST_DIR/test_output.csv" "$TEST_DIR/$dirname/out.csv")
    diff_result=$?

    if [ $exit_code -ne 0 ]; then
        echo -e "  ${RED}❌ FAILED - Exit code $exit_code${RESET}"
        failed_tests=$((failed_tests+1))

    elif [ $diff_result -ne 0 ]; then
        echo -e "  ${RED}❌ FAILED - Output mismatch${RESET}"
        echo "$diff_output" | sed "s/^</${GREEN}</; s/^>/${RED}>/"

        failed_tests=$((failed_tests+1))

    else
        echo -e "  ${GREEN}✅ PASSED${RESET} (${test_time}s)"
        passed_tests=$((passed_tests+1))
    fi

    timing_rows+=("$dirname,$test_time")

    rm -f "$TEST_DIR/test_output.csv"
    echo ""
done

###############################################################################
# CLEANUP UDP MODE
###############################################################################
if [ "$ENGINE_MODE" = "udp" ]; then
    kill -9 $BIN_PID 2>/dev/null
    kill -9 $CAT_PID 2>/dev/null
    rm -f /tmp/output_fifo /tmp/all_output.txt
fi

###############################################################################
# SUMMARY
###############################################################################
end_time=$(date +%s)
total_time=$((end_time - start_time))

echo "════════════════════════════════════════════════"
echo -e "${BLUE}📊 TEST SUMMARY${RESET}"
echo "════════════════════════════════════════════════"
echo ""
echo "  Total Tests:  $((passed_tests + failed_tests))"
echo -e "  Passed:       ${GREEN}$passed_tests${RESET}"
echo -e "  Failed:       ${RED}$failed_tests${RESET}"
echo "  Duration:     ${total_time}s"
echo ""

###############################################################################
# PER-TEST TIMING TABLE
###############################################################################
echo -e "${BLUE}⏱️  Per-Test Timing${RESET}"
printf "%-10s %-10s\n" "Test" "Seconds"
printf "%-10s %-10s\n" "-----" "-------"

for row in "${timing_rows[@]}"; do
    IFS=',' read -r name t <<< "$row"
    printf "%-10s %-10s\n" "$name" "$t"
done

rm -f /tmp/output_fifo

echo ""

###############################################################################
# REPORT GENERATION (added from run_tests.sh)
###############################################################################

REPORT="$TEST_DIR/report.xml"
HTML_REPORT="$TEST_DIR/report.html"

# Generate JUnit XML
cat > "$REPORT" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="MatchingEngineTests" tests="$((passed_tests + failed_tests))" failures="$failed_tests" errors="0" time="$total_time" timestamp="$(date -Iseconds)">
EOF

for row in "${timing_rows[@]}"; do
    IFS=',' read -r name t <<< "$row"
    #if grep -q "^  ${GREEN}✅" <<< "$(grep -A1 "Test Case: $name" /tmp/all_output.txt 2>/dev/null)"; then
    if [ $diff_result -eq 0 ]; then
        echo "    <testcase name=\"test_$name\" classname=\"MatchingEngineTests\" time=\"$t\"/>" >> "$REPORT"
    else
        echo "    <testcase name=\"test_$name\" classname=\"MatchingEngineTests\" time=\"$t\">" >> "$REPORT"
        echo "      <failure message=\"Test failed\" type=\"AssertionError\">Output mismatch</failure>" >> "$REPORT"
        echo "    </testcase>" >> "$REPORT"
    fi
done

cat >> "$REPORT" << EOF
  </testsuite>
</testsuites>
EOF

# Generate HTML report
if command -v junit2html &> /dev/null; then
    junit2html "$REPORT" "$HTML_REPORT" 2>/dev/null || true
fi

echo "════════════════════════════════════════════════"
echo "📄 Reports generated:"
echo "   - JUnit XML: $REPORT"
if [ -f "$HTML_REPORT" ]; then
    echo "   - HTML:      $HTML_REPORT"
fi
echo ""
