
# Order Book Matching Engine

This project implements a multi‑threaded, UDP‑based orderbook matching engine server.
It is designed to run inside the provided Docker environment and conform to the test harness.

The engine:

- Listens on UDP port 1234 (hardcoded)
- Processes CSV‑formatted commands
- Maintains per‑symbol price‑time‑priority order books
- Produces acknowledgements, trades, and top‑of‑book updates on stdout
- Supports limit orders, market (IOC - Immediate or Cancel) orders, partial fills, cancels, and flush operations

------------------------------------------------------------
## Requirements Summary

- Binary name: matching_engine
- Must listen on UDP port 1234
- Must write all output to stdout
- Must run inside the provided Docker environment
  (but can also be run manually on Linux/macOS)

------------------------------------------------------------
## Performance Notes
There is difference between running the binary directly and running it inside Docker:
# Native run: effectively instantaneous (the matching engine itself completes in under a few milliseconds)
# Docker run: typically 2–3 seconds
This difference is not due to the matching engine.
The extra time comes from Docker startup overhead, which includes:
-creating container namespaces
-setting up cgroups
-initializing the filesystem overlay
-configuring the network namespace
-tearing down the container when using --rm

The matching engine’s execution time inside the container is still extremely fast; the delay is entirely due to container startup and teardown.
------------------------------------------------------------
## Build Instructions (Docker)

### Build inside Docker (recommended)

    # Build image
    docker build -t matching_engine_image .

This compiles the engine inside Ubuntu 24.04 using CMake.
The resulting binary is located at:

    /build/matching_engine

------------------------------------------------------------
## Testing with Docker

### 1. Automated Testing (Docker)

    1.1 Generate Report as well
    docker run --rm -v "$(pwd)/reports:/reports" matching_engine_image

    1.2. Without report generated
    docker run --rm matching_engine_image

The test harness inside the container (run_tests.sh) will:

- Start the engine (UDP port 1234)
- Send test inputs from /test/<TestCaseFolder>/in.csv
- Capture stdout
- Compare against /test/<TestCaseFolder>/out.csv

It produces:

- Terminal summary
- reports/report.xml
- reports/report.html (if junit2html is installed)

------------------------------------------------------------
### 2. Manual Testing (inside Docker)

    docker run --rm -it --entrypoint /bin/bash matching_engine_image
    /build/matching_engine &
    cat /test/1/in.csv | nc -u 127.0.0.1 1234

Output appears on stdout.

------------------------------------------------------------
## Local Development (Optional)

    rm -rf build;
    mkdir build;
    cd build;
    cmake ..;
    cmake --build .;
    cd ..;

------------------------------------------------------------
## Run & Test Locally

### Terminal 1

    ./build/matching_engine

### Terminal 2

    cat test/1/in.csv | nc -u 127.0.0.1 1234
    #where nc = netcat (tool to send/receive data, open UDP/TCP connection, act like simple client/server)
    # -u = UDP [ without -u, it's TCP by default ]
    # nc -u 127.0.0.1 1234 => Send the data using UDP, (not TCP) to destination IP address (localhost = 127.0.0.1 = same machine) # at port 1234

------------------------------------------------------------
## Local Automated Testing (run_tests.sh)

A standalone local test harness is provided at:

    test/run_tests.sh 

### Usage

1. Ensure no instance of matching_engine is bound to UDP port 1234.
2. Ensure each test directory contains in.csv and out.csv.
3. Run:

       ./test/run_tests.sh --mode udp --bin ./build/matching_engine

Note: Running in STDIN mode may not work as matching engine expects to run in udp mode.
      test/run_tests.sh  [by default STDIN ]==> May not work

### How It Works

- Starts engine in background
- Sends each line of in.csv via /dev/udp/127.0.0.1/1234
- Captures stdout into /tmp/all_output.txt
- Extracts new lines into test_output.csv
- Compares test_output.csv with expected out.csv
- Prints summary and timing

------------------------------------------------------------
## Project Structure


CMakeLists.txt
src/
    MatchingEngineMain.cpp
    MatchingEngine.hpp
    MatchingEngine.cpp
    SymbolOrderbook.hpp
    SymbolOrderbook.cpp
    util/
      StringUtil.hpp
      StringUtil.cpp
      ThreadSafeQueue.hpp
    domain/
      types.hpp
      Order.hpp
      OrderKey.hpp
      TopOfBook.hpp
      commandParser.hpp
      commandParser.cpp
    memory/
      OrderSlotPool.hpp
      OrderSlotPool.cpp

test/
  <TestCaseFolder>/in.csv
  <TestCaseFolder>/out.csv
Dockerfile
README.md
DESIGN.md

------------------------------------------------------------
## Features

- Deterministic, iterator‑based matching engine
- Per‑symbol price‑time‑priority order books
- Market (IOC), limit, partial fills, cancels, flush
- Reuses cancelled and fully filled order slots through a dedicated slot‑pool allocator to prevent unbounded growth of the order  
  container (deque<Order>) while preserving iterator stability

- UDP input, stdout output
- Fully automated test harness (Docker + local)

### Order Slot Reuse

The matching engine maintains stable iterators for all resting orders.  
To prevent unbounded growth of the underlying deque that stores orders, a slot‑pool allocator is used.  
This allocator recycles slots belonging to cancelled or fully filled orders.  
Recycling is safe because no erasure occurs in the main container, and all references from price levels and lookup structures are removed before a slot is returned to the pool.


------------------------------------------------------------
## Limitations

- No persistence or recovery
- No authentication or rate limiting
- Single‑process engine (multi‑threaded input/output only)
- Slot reuse does not compact memory; it only prevents further growth of the order container


