
# Design Overview

This document describes the architecture, data structures, design decisions,
and potential improvements for the order book matching engine.

---

## 1. Architecture

### Thread Model

The engine uses a **three‑thread pipeline** to ensure deterministic behavior and
clear separation of responsibilities:

1. **Input Thread**
   - Binds UDP port 1234
   - Receives datagrams
   - Splits into newline‑separated commands
   - Parses each into a `Command` structure
   - Pushes commands into a thread‑safe queue

2. **Processing Thread**
   - Consumes parsed commands in FIFO order
   - Maintains all per‑symbol order books
   - Executes matching logic
   - Generates output lines (acknowledgements, trades, top‑of‑book updates)
   - Pushes output lines into a second thread‑safe queue

3. **Output Thread**
   - Consumes output lines
   - Writes them to stdout in strict FIFO order
   - Flushes after each line to satisfy the test harness

### Synchronization Strategy

- Two ThreadSafeQueue<T> instances:
  - inputQueue: input thread → processing thread
  - outputQueue: processing thread → output thread
- Each queue uses:
  - std::mutex
  - std::condition_variable
  - FIFO std::deque<T>
- No locks are required inside the order book because only the processing
  thread mutates engine state.

### Determinism and Isolation

The architecture isolates concerns:

- UDP parsing is independent from matching logic.
- Matching logic is independent from output formatting.
- Output is serialized through a dedicated thread to avoid interleaving.
- Only one thread mutates engine state, eliminating race conditions.

This structure ensures deterministic, reproducible behavior.

---

## 2. Data Structures

### Order Book Representation

Each symbol has a SymbolOrderbook instance containing:

- Bid side  
  std::vector<PriceLevel> sorted in descending price order

- Ask side  
  std::vector<PriceLevel> sorted in ascending price order

- Order storage  
  std::deque<Order>  
  Provides stable iterators for the lifetime of each order.

- Order lookup  
  std::unordered_map<OrderKey, OrderIterator>  
  Enables O(1) average‑time cancel operations.

- Order Slot Pool
  A slot‑pool allocator is used to recycle storage for cancelled or fully filled
  orders. The main order container (std::deque<Order>) is never erased from, so
  iterators remain stable for all resting orders. When an order becomes inactive,
  its iterator is returned to the slot pool. New resting orders reuse these slots
  instead of pushing new elements into the deque. This prevents unbounded growth
  of the order container and maintains iterator stability across all price levels.

- PriceLevel structure  
  Contains:
  - price
  - totalQuantity
  - std::deque<OrderIterator> preserving FIFO time priority

- Top‑of‑book snapshot  
  Stored to detect changes and emit B‑messages.

### Rationale for Sorted Vectors

Sorted vectors were chosen instead of std::map because they provide:

- Better cache locality
- Faster iteration during matching
- Lower memory overhead
- Simple binary search for price‑level insertion
- No iterator invalidation issues for stored orders

### Time Complexity

| Operation              | Complexity | Notes                               |
|------------------------|-----------:|-------------------------------------|
| Insert limit order     |   O(log P) | binary search on price levels       |
| Cancel order           | O(1) avg   | hash lookup + O(1) deque erase      |
| Match at best price    | O(1) amort | deque front pop                     |
| Walk multiple levels   |      O(K)  | K = number of crossed levels        |
| Compute top‑of‑book    |      O(1)  | first element of vector             |

### Space Complexity

- O(N) total orders stored across all price levels  
- O(N) for hash lookup table  
- O(P) price levels, where P ≤ N  

---

## 3. Design Decisions

### Price‑Time Priority

- Sorted vectors maintain price priority.
- std::deque maintains FIFO time priority within each price level.
- Stable iterators ensure safe cancellation and modification.

### Market Orders as IOC

- Market orders never join the book.
- They walk price levels until filled or exhausted.

### Slot Reuse and Iterator Stability

The matching engine maintains stable iterators for all resting orders. To avoid
unbounded growth of the underlying deque, a slot‑pool allocator recycles slots
belonging to cancelled or fully filled orders. Recycling is safe because all
references from price levels and lookup structures are removed before a slot is
returned to the pool. This approach preserves iterator stability while keeping
memory usage bounded.

### No Self‑Matching

- Incoming orders skip resting orders with the same userId.

### Top‑of‑Book Emission Rules

A B‑message is emitted when:

- A side becomes non‑empty  
- Best price changes  
- Best quantity changes  
- A side becomes empty (`-, -`)  

These rules match the expected behavior defined by the test harness.

### Thread Isolation

Only the processing thread mutates engine state.  
This eliminates the need for fine‑grained locking inside the order book.

### UDP Bind Retry Logic

The input thread retries bind() several times because the OS may briefly
retain port 1234 after a previous run.  
This improves robustness without affecting behavior.

---

## 4. Project Structure

submission/
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
  CMakeLists.txt
test/
  <id>/in.csv
  <id>/out.csv
Dockerfile
README.md
DESIGN.md

---

## 5. Improvements

Several enhancements could be implemented with additional time:

### Performance
- Replace sorted vectors with a flat‑tree or skip‑list for improved locality.
- Use memory pools for order allocation to reduce fragmentation.
- Implement batch processing for UDP packets.

### Features
- Support for order modifications (replace).
- Support for persistent order IDs across flushes.
- Add metrics and instrumentation.

### Robustness
- Add malformed input detection and structured error reporting.
- Add unit tests for each matching scenario.

### Concurrency
- Introduce lock‑free queues for lower latency.
- Add optional multi‑symbol parallelism.

---

This design emphasizes correctness, determinism, and clarity while remaining
fully compatible with the test harness.
