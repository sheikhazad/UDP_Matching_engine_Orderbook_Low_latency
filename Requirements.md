**Project:**
Implement a **multi-threaded** order book / matching engine system that:
- Accepts input via **UDP protocol**
- Maintains multiple price-time order books (one per symbol)
- **Matches and trades** orders that cross the book
- Supports **market orders** (price = 0) with IOC (Immediate or Cancel) semantics
- Handles **partial quantity matches**


**Requirements:**
- Software produce a binary named `matching_engine`
- Must listen on **UDP port 1234** (hardcoded)
- Must accept CSV commands via UDP
- Must write CSV output to **stdout**

## Requirements

### Input - UDP Protocol

This program accepts input via **UDP on port 1234** (hardcoded).

The input uses CSV format with three command types:

**New Order:**
```csv
N, userId, symbol, price, quantity, side, userOrderId
```

**Cancel Order:**
```csv
C, userId, userOrderId
```

**Flush (reset all order books):**
```csv
F
```

**Market Orders:** When `price = 0`, the order is a market order that executes immediately at the best available price(s). Any unmatched portion is canceled (IOC - Immediate or Cancel).

### Order Book Logic

The order book uses **price-time priority**.

**Market Orders (price = 0):**
- Execute immediately against the opposite side at best available price(s)
- Any unmatched portion is canceled (IOC - Immediate or Cancel)
- Do not join the book

**Limit Orders (price > 0):**
- If they cross the book, match immediately (partial matches allowed)
- Any remaining quantity joins the book in price-time priority
- Sit on the book until matched or canceled

**Partial Matches:**
- Orders can be partially filled
- Multiple trades may result from a single order
- Remaining quantity stays on book (for limit orders)

### Multi-threading Requirements

Program must use separate threads for:

1. **Input Thread**: Buffer incoming UDP messages
2. **Processing Thread**: Process orders and maintain book state
3. **Output Thread**: Publish results to stdout

This architecture ensures network I/O doesn't block order processing.

### Output - stdout

Program must publishe to **stdout** using CSV format:

**Order acknowledgement:**
```csv
A, userId, userOrderId
```

**Cancel acknowledgement:**
```csv
C, userId, userOrderId
```

**Trade (matched orders):**
```csv
T, userIdBuy, userOrderIdBuy, userIdSell, userOrderIdSell, price, quantity
```

**Top of book change:**
```csv
B, side (B or S), price, totalQuantity
```
- Use `-` for price and totalQuantity when a side is eliminated

**Important:** Output must be thread-safe. Multiple threads writing to stdout must not interleave.

### Test Scenarios

The test suite must include multiple scenarios covering:
- Balanced book operations
- Market orders (price = 0)
- Partial fills
- Multiple trades from single order
- Limit orders that cross and partially fill
- Multi-symbol books
- Cancellations
- Edge cases

Expected output
  - Generate the correct outputs for these scenarios
  

## Build Requirements

Application must:
- Produce a binary named `matching_engine`
- Listen on UDP port 1234 (hardcoded)
- Write output to stdout
- Work in the provided Docker environment (Ubuntu 24.04, x64)

## Testing

### Automated Testing

Run tests using Docker (works on all platforms):

```bash
# Build and run tests (reports saved to ./reports/)
docker build -t matching_engine_image .
docker run --rm -v "$(pwd)/reports:/reports" matching_engine_image
```

Test results are displayed in the terminal and saved to `./reports/`.

The test framework will:
1. Start program listening on port 1234
2. Send test data via UDP
3. Collect output from stdout
4. Compare with expected output

### Manual Testing

**Inside Docker (recommended):**
```bash
# Build the Docker image
docker build -t matching_engine_image .

# Run a shell inside the container
docker run --rm -it --entrypoint /bin/bash matching_engine_image

# Inside container - start program
/build/matching_engine &

# Send test data (requires BSD netcat)
cat /test/1/in.csv | nc -u 127.0.0.1 1234
```

**Local development:**
```bash
# Build solution
mkdir build && cd build
cmake .. && cmake --build .

# Run (listens on port 1234)
./matching_engine

# In another terminal, send test data (requires BSD netcat):
cat test/1/in.csv | nc -u 127.0.0.1 1234
```


### DESIGN.md

Create a `DESIGN.md` file explaining:
- **Architecture**: Thread design, synchronization strategy
- **Data structures**: Order book implementation, [time complexity](https://en.wikipedia.org/wiki/Time_complexity) and [space complexity](https://en.wikipedia.org/wiki/Space_complexity)
- **Design decisions**: Trade-offs made, why we chose specific approaches
- **Project structure**: File organization, key components
- **Improvements**: What we can do with more time?

## Tips - Approach

1. **Start simple**: Get basic UDP input/output working first
2. **Test incrementally**: Run the Docker tests frequently
3. **Generate even outputs early**: Don't leave this to the end
4. **Handle threading carefully**: Use proper synchronization (mutexes, channels)
5. **Document as we go**: Write DESIGN.md while implementing

## Verification

When complete:

1. **Ensure ALL tests pass** - Run the Docker tests to verify
2. Create a git bundle:
   ```bash
   git add --all
   git commit -m "Matching Engine"
   git bundle create matching_engine.bundle master
   ```
3.  `.bundle` is ready

