//OrderSlotPool.hpp
#pragma once

#include <deque>
#include <vector>
#include "domain/Order.hpp"

/**
 * OrderSlotPool
 * ---------------------------------------------------------------------------
 * Memory‑reuse helper for SymbolOrderbook.
 *
 * Purpose:
 *   - Avoid unbounded growth of the main OrderContainer (std::deque<Order>)
 *   - Reuse iterators of cancelled or fully‑filled orders
 *   - Preserve iterator stability (std::deque guarantees this)
 *
 * Context:
 *   The matching engine does not erase from the main deque<Order> because
 *   erasing would invalidate iterators stored in price levels. Instead,
 *   filled or cancelled orders become "dead slots". This pool recycles those
 *   slots for new orders, eliminating memory bloat and improving cache
 *   locality.
 *
 * Design:
 *   - freeList_ stores iterators to reusable Order objects
 *   - allocate() returns either a recycled iterator or a new one
 *   - release() pushes an iterator back into the free list
 *
 * Threading:
 *   - SymbolOrderbook is single‑threaded by design
 *   - No locking required
 */
class OrderSlotPool {
public:
    using OrderContainer = std::deque<Order>;
    using OrderIterator  = OrderContainer::iterator;

    /**
     * Constructor.
     *
     * @param storage Reference to the order container owned by SymbolOrderbook.
     *
     * Rationale:
     *   The pool does not own storage; it only manages iterator reuse.
     *   This keeps ownership clear and avoids double‑free risks.
     */
    explicit OrderSlotPool(OrderContainer& storage)
        : storage_(storage) {}

    /**
     * allocate()
     * -----------------------------------------------------------------------
     * Returns an iterator to an Order object that can be safely overwritten.
     *
     * Behavior:
     *   - If freeList_ has recycled slots, reuse one (fast path).
     *   - Otherwise, push_back() a new Order into storage_ (slow path).
     *
     * Complexity:
     *   - O(1) amortized.
     *
     * Iterator stability:
     *   - std::deque guarantees that push_back() does not invalidate existing
     *     iterators, making this safe for the entire orderbook.
     */
    OrderIterator allocate();

    /**
     * release()
     * -----------------------------------------------------------------------
     * Marks an iterator as reusable.
     *
     * @param it Iterator to a previously used Order object.
     *
     * Behavior:
     *   - Caller must ensure the order is no longer referenced by any
     *     price level or cancel map.
     *   - The Order object will be overwritten on next allocation.
     */
    void release(OrderIterator it);

private:
    OrderContainer& storage_;             // Reference to SymbolOrderbook::orders_
    std::vector<OrderIterator> freeList_; // LIFO stack of reusable slots
};
