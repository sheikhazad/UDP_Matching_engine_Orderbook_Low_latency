//OrderSlotPool.cpp
#include "OrderSlotPool.hpp"

/**
 * allocate()
 * ---------------------------------------------------------------------------
 * Returns an iterator to a usable Order slot.
 *
 * Fast path:
 *   - Reuse a previously freed iterator from freeList_.
 *
 * Slow path:
 *   - Append a new Order{} to storage_ and return its iterator.
 *
 * LIFO rationale:
 *   - Better cache locality: recently freed slots are likely still warm.
 *   - No ordering constraints: any free slot is equally valid.
 */
OrderSlotPool::OrderIterator OrderSlotPool::allocate() {
    if (!freeList_.empty()) {
        OrderIterator it = freeList_.back();
        freeList_.pop_back();
        return it;
    }

    // No free slots → append a new Order.
    storage_.push_back(Order{});
    return std::prev(storage_.end());
}

/**
 * release()
 * ---------------------------------------------------------------------------
 * Adds an iterator to the free list for future reuse.
 *
 * Preconditions:
 *   - The order is no longer referenced by:
 *       * any price level deque
 *       * ordersByKey_ cancel map
 *
 * Postconditions:
 *   - The Order object will be overwritten on next allocate().
 */
void OrderSlotPool::release(OrderIterator it) {
    freeList_.push_back(it);
}
