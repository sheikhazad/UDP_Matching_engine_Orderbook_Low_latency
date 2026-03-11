#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/types.hpp"
#include "domain/TopOfBook.hpp"
#include "domain/Order.hpp"
#include "domain/OrderKey.hpp"

//For memory management of orders, to avoid unbounded growth of the main deque and to reuse slots of cancelled 
//or filled orders without invalidating iterators.
#include "memory/OrderSlotPool.hpp"


/**
 * Order book for a single symbol using price–time priority.
 *
 * Rationale:
 *   This structure maintains two sorted sides (bids and asks) and stores
 *   all orders in a deque so that iterators remain stable for the lifetime
 *   of the book. Price levels store iterators to orders rather than copies,
 *   allowing efficient matching, cancellation, and top‑of‑book updates.
 *
 * Invariants:
 *   - Bids are sorted in descending price order.
 *   - Asks are sorted in ascending price order.
 *   - Order iterators remain valid because the main container never erases.
 *   - totalQuantity in each price level always reflects the sum of remaining
 *     quantities of all orders in that level.
 *
 * Complexity:
 *   - Price‑level lookup: O(log L) [where 𝐿 is the number of price levels (not num of orders) on that side] via binary search.
 *   - Matching: 𝑂(𝑀) where 𝑀 is the number of matches performed.
 *   - Cancel: O(1) average via iterator lookup.
 *             average 𝑂(1) via ordersByKey_ lookup + 𝑂(1) removal from the price level.
 */
class SymbolOrderbook {
public:
    /**
     * Constructs an empty order book.
     *
     * Rationale:
     *   Initializes all internal structures to empty states. No dynamic
     *   allocation is required because all containers manage their own memory.
     */
    SymbolOrderbook();

    /**
     * Processes a new order (limit or market) and appends output lines.
     *
     * Rationale:
     *   New orders may match immediately or rest in the book. This function
     *   delegates to the appropriate matching routine based on side and price.
     *
     * Invariants:
     *   - Emits an acknowledgement line before processing.
     *   - Updates top‑of‑book after all matching is complete.
     */
    void processNewOrder(const Command& cmd,
                         std::vector<std::string>& outputLines);

    /**
     * Processes a cancel command and appends output lines.
     *
     * Rationale:
     *   Cancels remove resting orders from their price level while preserving
     *   iterator stability. The order remains in the main deque but is marked
     *   by setting its quantity to zero.
     *
     * Invariants:
     *   - If the order exists, its remaining quantity is removed from the
     *     price level’s running total.
     *   - Top‑of‑book is updated after cancellation.
     */
    void processCancel(const Command& cmd,
                       std::vector<std::string>& outputLines);

    /**
     * Removes all orders and resets the book to an empty state.
     *
     * Rationale:
     *   Used for a global flush. All containers are cleared and sequence
     *   counters reset. Iterator stability is irrelevant after this call.
     */
    void flush();

private:
    using OrderContainer = std::deque<Order>;
    using OrderIterator = OrderContainer::iterator;

    /**
     * PriceLevel = All orders at price X, in the order they arrived
     * Represents a single price level containing:
     *   - price: the level price
     *   - totalQuantity: sum of all remaining quantities at this level
     *   - orders: FIFO queue of iterators preserving time priority
     *
     * Rationale:
     *   Keeping totalQuantity updated allows O(1) top‑of‑book queries.
     *  Storing iterators avoids order copies and preserves stability.
     * 
     * Example:
     * Internal OrderContainer (actual orders)
     * Index   Order
     * -------------------------
     * 0       {id=A, qty=100, price=101}
     * 1       {id=B, qty=150, price=101}
     * 2       {id=C, qty=50,  price=100}
     * 
     * PriceLevel for price 101
     * price = 101
     * totalQuantity = 250
     * orders = [ iterator->Order0, iterator->Order1 ]
     * 
     * What is std::deque<OrderIterator> orders?
     * This is the FIFO queue of iterators to the actual order objects,so we can match them in time order without copying anything.”
     * Always match from orders.front()
     * Always append new orders at orders.back()
     * Cancels remove a specific iterator [not actual order inside orders_ ] from the deque, and update totalQuantity accordingly.
     *
     * Why iterators instead of copying Orders?
     * Because:
     * Orders (orders_) live in a central container (OrderContainer = std::deque<Order>)
     * We don’t want to duplicate them
     * We want stable references (deque iterators don’t invalidate on push_back)
     * We want to modify or cancel the order later
     * 
     * Orderbook
     * ├── Price Levels (grouped by price)
     * │     ├── FIFO queue of orders (iterators)
     * │     └── total quantity at that price
     * └── Actual Order Storage (deque<Order>  orders_)

     */

     //Even in a single-threaded engine, cache-line alignment can help locality and predictability
     //Modern CPUs have hardware prefetchers that work better with aligned data structures.
    //struct  alignas(std::hardware_destructive_interference_size)*/ 
    struct alignas(64) PriceLevel {
        std::int64_t price{0};
        std::int64_t totalQuantity{0};
        //orders = [iterator_to_OrderA, iterator_to_OrderB, iterator_to_OrderC]
        std::deque<OrderIterator> orders; //FIFO queue of orders at X price
    };

    // Bid side: sorted descending by price.
    std::vector<PriceLevel> bids_;

    // Ask side: sorted ascending by price.
    std::vector<PriceLevel> asks_;

    // Main storage for all orders. Iterators remain stable.
    OrderContainer orders_;

    // Fast lookup from (userId, userOrderId) to order iterator.
    std::unordered_map<OrderKey, OrderIterator, OrderKeyHash> ordersByKey_;

    // Cached top‑of‑book snapshots.
    TopOfBook currentTopBid_;
    TopOfBook currentTopAsk_;

    // Monotonic sequence number (1,2,3 ...) for time priority.
    std::uint64_t nextSequence_{1};

    //New Addition:
    // Manages reusable order slots to prevent unbounded growth of orders_.
    // Slots belonging to cancelled or fully filled orders can be reused without
    // invalidating iterators, because orders_ is never erased from.
    // Reuse is safe only after all price levels and lookup structures have removed
    // references to the old order.
    OrderSlotPool slotPool_;



    /**
     * Finds the index of a bid price level using binary search.
     *
     * Rationale:
     *   Bids are sorted descending, so the comparison is reversed.
     *
     * Returns:
     *   - index where the price exists, or where it should be inserted.
     *   - sets 'found' to true if the price level exists.
     */
    std::size_t findBidLevelIndex(std::int64_t price, bool& found) const;

    /**
     * Finds the index of an ask price level using binary search.
     *
     * Rationale:
     *   Asks are sorted ascending, so standard binary search applies.
     */
    std::size_t findAskLevelIndex(std::int64_t price, bool& found) const;

    /**
     * Retrieves or creates a bid price level at the correct sorted position.
     *
     * Rationale:
     *   Ensures price‑level invariants(rules) remain intact after insertion.
     */
    PriceLevel& getOrCreateBidLevel(std::int64_t price);

    /**
     * Retrieves or creates an ask price level at the correct sorted position.
     */
    PriceLevel& getOrCreateAskLevel(std::int64_t price);

    /**
     * Removes a bid level if it contains no orders.
     *
     * Rationale:
     *   Keeps the bid side compact and ensures top‑of‑book correctness.
     */
    void removeEmptyBidLevel(std::size_t index);

    /**
     * Removes an ask level if it contains no orders.
     */
    void removeEmptyAskLevel(std::size_t index);

    /**
     * Computes the current top bid snapshot.
     *
     * Rationale:
     *   Uses running totals for O(1) access.
     */
    TopOfBook computeTopBid() const;

    /**
     * Computes the current top ask snapshot.
     */
    TopOfBook computeTopAsk() const;

    /**
     * Compares two top‑of‑book snapshots for equality.
     *
     * Rationale:
     *   Used to determine whether an update should be emitted.
     */
    static bool topEqual(const TopOfBook& a, const TopOfBook& b);

    /**
     * Updates cached top‑of‑book and emits changes if necessary.
     *
     * Rationale:
     *   Ensures that only actual changes generate output lines.
     */
    void updateTopOfBookAndEmitChanges(const TopOfBook& oldBid,
                                       const TopOfBook& oldAsk,
                                       std::vector<std::string>& outputLines);

    /**
     * Emits a trade line for a matched buy and sell order.
     *
     * Rationale:
     *   Centralized formatting ensures consistency across all match paths.
     */
    void emitTrade(const Order& buy,
                   const Order& sell,
                   std::int64_t tradePrice,
                   std::int64_t tradeQty,
                   std::vector<std::string>& outputLines);

    /**
     * Constructs an Order from a Command.
     *
     * Rationale:
     *   Used for incoming orders before matching or resting.
     */
    static Order makeIncomingOrder(const Command& cmd);

    /**
     * Matches a market buy order against the best asks.
     *
     * Rationale:
     *   Market orders execute immediately at the best available price.
     *
     * Behavior:
     *   - Walks ask levels from lowest price upward.
     *   - Skips self‑matching.
     *   - Updates running totals and removes filled orders.
     */
    void matchMarketBuy(const Command& cmd,
                        std::vector<std::string>& outputLines);

    /**
     * Matches a market sell order against the best bids.
     */
    void matchMarketSell(const Command& cmd,
                         std::vector<std::string>& outputLines);

    /**
     * Matches a limit buy order and rests any remaining quantity.
     *
     * Rationale:
     *   Limit orders match only at prices equal to or better than their limit.
     */
    void matchLimitBuy(const Command& cmd,
                       std::vector<std::string>& outputLines);

    /**
     * Matches a limit sell order and rests any remaining quantity.
     */
    void matchLimitSell(const Command& cmd,
                        std::vector<std::string>& outputLines);
};
