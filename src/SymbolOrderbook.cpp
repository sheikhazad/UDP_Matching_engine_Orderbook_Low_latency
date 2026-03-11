#include "SymbolOrderbook.hpp"


#include <algorithm>
#include <sstream>

/**
 * Constructs an empty order book.
 *
 * Rationale:
 *   All containers start empty. No dynamic allocation is required because
 *   std::deque and std::vector manage their own memory. Sequence numbers
 *   begin at 1 to preserve strict ordering for resting orders.
 */
//SymbolOrderbook::SymbolOrderbook() = default;
SymbolOrderbook::SymbolOrderbook()
    : slotPool_(orders_) {}


/**
 * Finds the index of a bid price level using binary search.
 *
 * Rationale:
 *   Bid levels are sorted in descending price order. This function performs
 *   a binary search with reversed comparison logic to locate the correct
 *   position for a given price.
 *   Method is implementing lower_bound semantics on a sorted bid‑side price ladder
 * Invariants:
 *   - If 'found' is set to true, bids_[index].price == price.
 *   - If 'found' is false, the returned index is the correct insertion point.
 *
 * Complexity:
 *   O(log N) where N is the number of bid price levels.
 */
std::size_t SymbolOrderbook::findBidLevelIndex(std::int64_t incomingPrice, bool& found) const {
    std::size_t left = 0;
    std::size_t right = bids_.size();
    found = false;

    while (left < right) {
        const std::size_t mid = (left + right) / 2;
        if (bids_[mid].price == incomingPrice) {
            found = true;
            return mid;
        }
        if (bids_[mid].price > incomingPrice) {
            left = mid + 1;  // search lower priority (smaller index)
        } else {
            //not right = mid - 1; as explained in findAskLevelIndex()
            right = mid;     // search higher priority (larger price)
        }
    }
    return left;
}

/**
 * Finds the index of an ask price level using binary search.
 *
 * Rationale:
 *   Ask levels are sorted in ascending price order. Standard binary search
 *   applies directly.
 *   Method is implementing lower_bound semantics on a sorted ask‑side price ladder.
 * Complexity:
 *   O(log N) where N is the number of ask price levels.
 */
std::size_t SymbolOrderbook::findAskLevelIndex(std::int64_t incomingPrice, bool& found) const {
    std::size_t left = 0;
    std::size_t right = asks_.size();
    found = false;

    while (left < right) {
        const std::size_t mid = (left + right) / 2;
        if (asks_[mid].price == incomingPrice) {
            found = true;
            return mid;
        }
        if (asks_[mid].price < incomingPrice) {
            left = mid + 1;
        } else {
            //E.g. Ask ladder: [101, 102, 105, 110]
            // Incoming price: 103
            //mid = 2, asks_[2] = 105
            //105 > 103 → we must search left
            //right = mid - 1; then right = 1, So, inserting in fromt of ask_[1] = 102 is wrong.
            //We must do right = mid; then right = 2, so that we can insert at index 2, asks_[2] = 105
            right = mid;
        }
    }
    return left;
}

/**
 * Retrieves or creates a bid price level at the correct sorted position.
 *
 * Rationale:
 *   Ensures that the bid side remains sorted in descending order. If the
 *   price level already exists, it is returned. Otherwise, a new level is
 *   inserted at the correct position.
 */
SymbolOrderbook::PriceLevel& SymbolOrderbook::getOrCreateBidLevel(std::int64_t price) {
    bool found = false;
    const std::size_t idx = findBidLevelIndex(price, found);
    if (found) {
        return bids_[idx];
    }

    PriceLevel level;
    level.price = price;
    //std::ptrdiff_t = type of the result when we subtract one pointer from another.
    bids_.insert(bids_.begin() + static_cast<std::ptrdiff_t>(idx), level);
    return bids_[idx];
}

/**
 * Retrieves or creates an ask price level at the correct sorted position.
 *
 * Rationale:
 *   Ensures that the ask side remains sorted in ascending order.
 */
SymbolOrderbook::PriceLevel& SymbolOrderbook::getOrCreateAskLevel(std::int64_t price) {
    bool found = false;
    const std::size_t idx = findAskLevelIndex(price, found);
    if (found) {
        return asks_[idx];
    }

    PriceLevel level;
    level.price = price;
    asks_.insert(asks_.begin() + static_cast<std::ptrdiff_t>(idx), level);
    return asks_[idx];
}

/**
 * Removes a bid level if it contains no orders.
 *
 * Rationale:
 *   Keeps the bid side compact and ensures that top‑of‑book queries remain
 *   accurate. Empty levels serve no purpose and must be removed.
 */
void SymbolOrderbook::removeEmptyBidLevel(std::size_t index) {
    if (index < bids_.size() && bids_[index].orders.empty()) {
        bids_.erase(bids_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

/**
 * Removes an ask level if it contains no orders.
 *
 * Rationale:
 *   Same reasoning as removeEmptyBidLevel.
 */
void SymbolOrderbook::removeEmptyAskLevel(std::size_t index) {
    if (index < asks_.size() && asks_[index].orders.empty()) {
        asks_.erase(asks_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

/**
 * Computes the current top bid snapshot.
 *
 * Rationale:
 *   The top bid is the first element of the bid vector because bids are
 *   sorted in descending price order. Running totals allow O(1) access.
 */
TopOfBook SymbolOrderbook::computeTopBid() const {
    TopOfBook tob;
    if (!bids_.empty()) {
        const auto& level = bids_.front();
        tob.valid = true;
        tob.price = level.price;
        tob.totalQuantity = level.totalQuantity;
    }
    return tob;
}

/**
 * Computes the current top ask snapshot.
 *
 * Rationale:
 *   The top ask is the first element of the ask vector because asks are
 *   sorted in ascending price order.
 */
TopOfBook SymbolOrderbook::computeTopAsk() const {
    TopOfBook tob;
    if (!asks_.empty()) {
        const auto& level = asks_.front();
        tob.valid = true;
        tob.price = level.price;
        tob.totalQuantity = level.totalQuantity;
    }
    return tob;
}

/**
 * Compares two top‑of‑book snapshots for equality.
 *
 * Rationale:
 *   Used to determine whether a top‑of‑book update should be emitted.
 */
bool SymbolOrderbook::topEqual(const TopOfBook& a, const TopOfBook& b) {
    if (a.valid != b.valid) {
        return false;
    }
    if (!a.valid) {
        return true;
    }
    return a.price == b.price && a.totalQuantity == b.totalQuantity;
}

/**
 * Updates cached top‑of‑book and emits changes if necessary.
 *
 * Rationale:
 *   Only actual changes should generate output lines. This function compares
 *   old and new snapshots and emits updates accordingly.
 */
void SymbolOrderbook::updateTopOfBookAndEmitChanges(const TopOfBook& oldBid,
                                                    const TopOfBook& oldAsk,
                                                    std::vector<std::string>& outputLines) {
    const TopOfBook newBid = computeTopBid();
    const TopOfBook newAsk = computeTopAsk();

    currentTopBid_ = newBid;
    currentTopAsk_ = newAsk;

    if (!topEqual(oldBid, newBid)) {
        std::ostringstream oss;
        //Output format for top‑of‑book update: B, side (B or S), price, totalQuantity
        //- Use `-` for price and totalQuantity when a side is eliminated
        oss << "B, B, ";
        if (!newBid.valid) {
            oss << "-, -";
        } else {
            oss << newBid.price << ", " << newBid.totalQuantity;
        }
        outputLines.push_back(oss.str());
    }

    if (!topEqual(oldAsk, newAsk)) {
        std::ostringstream oss;
        oss << "B, S, ";
        if (!newAsk.valid) {
            oss << "-, -";
        } else {
            oss << newAsk.price << ", " << newAsk.totalQuantity;
        }
        outputLines.push_back(oss.str());
    }
}

/**
 * Emits a trade line for a matched buy and sell order.
 *
 * Rationale:
 *   Centralized formatting ensures consistent output across all match paths.
 */
void SymbolOrderbook::emitTrade(const Order& buy,
                                const Order& sell,
                                std::int64_t tradePrice,
                                std::int64_t tradeQty,
                                std::vector<std::string>& outputLines) {
    std::ostringstream oss;
    //Output format for trade(matched orders): T, buyUserId, buyUserOrderId, sellUserId, sellUserOrderId, tradePrice, tradeQuantity
    //Example: T, 1, 1001, 2, 2001, 101, 50
    oss << "T, "
        << buy.userId << ", " << buy.userOrderId << ", "
        << sell.userId << ", " << sell.userOrderId << ", "
        << tradePrice << ", " << tradeQty;
    outputLines.push_back(oss.str());
}

/**
 * Constructs an Order from a Command.
 *
 * Rationale:
 *   Incoming orders are represented as temporary Order objects before they
 *   are matched or rested. Sequence is set to zero because resting orders
 *   receive a new sequence number when inserted.
 */
Order SymbolOrderbook::makeIncomingOrder(const Command& cmd) {
    Order o;
    o.userId = cmd.userId;
    o.userOrderId = cmd.userOrderId;
    o.side = cmd.side;
    o.price = cmd.price;
    o.quantity = cmd.quantity;
    o.sequence = 0;
    return o;
}

/**
 * Processes a new order and appends outputlines.
 *
 * Rationale:
 *   Emits an acknowledgement line, then delegates to the appropriate
 *   matching routine based on side and price. After matching, updates
 *   top‑of‑book and emits changes if necessary.
 */
void SymbolOrderbook::processNewOrder(const Command& cmd,
                                      std::vector<std::string>& outputLines) {
    {
        std::ostringstream oss;
        //Output format for new order acknowledgement : A, userId, userOrderId
        oss << "A, " << cmd.userId << ", " << cmd.userOrderId;
        outputLines.push_back(oss.str());
    }

    const TopOfBook oldBid = currentTopBid_;
    const TopOfBook oldAsk = currentTopAsk_;

    if (cmd.price == 0) {
        if (cmd.side == Side::Buy) {
            matchMarketBuy(cmd, outputLines);
        } else {
            matchMarketSell(cmd, outputLines);
        }
    } else {
        if (cmd.side == Side::Buy) {
            matchLimitBuy(cmd, outputLines);
        } else {
            matchLimitSell(cmd, outputLines);
        }
    }

    updateTopOfBookAndEmitChanges(oldBid, oldAsk, outputLines);
}

/**
 * Processes a cancel command and appends output lines.
 *
 * Rationale:
 *   Cancels remove resting orders from their price level while preserving
 *   iterator stability. The order remains in the main deque but is marked
 *   by setting its quantity to zero.
 *
 * Behavior:
 *   - If the order exists, its remaining quantity is removed from the
 *     price level’s running total.
 *   - The iterator is removed from the price level’s deque.
 *   - The order is marked as inactive by setting quantity to zero.
 */
void SymbolOrderbook::processCancel(const Command& cmd,
                                    std::vector<std::string>& outputLines) {
    {
        std::ostringstream oss;
        //Output format for cancel acknowledgement: C, userId, userOrderId
        //Example: C, 1, 1001
        oss << "C, " << cmd.userId << ", " << cmd.userOrderId;
        outputLines.push_back(oss.str());
    }

    const TopOfBook oldBid = currentTopBid_;
    const TopOfBook oldAsk = currentTopAsk_;

    const OrderKey key{cmd.userId, cmd.userOrderId};
    const auto it = ordersByKey_.find(key);

    if (it != ordersByKey_.end()) {
        OrderIterator ordIt = it->second;
        Order& order = *ordIt;
        const std::int64_t price = order.price;
        const std::int64_t qty = order.quantity;

        if (qty > 0) {
            if (order.side == Side::Buy) {
                bool found = false;
                const std::size_t idx = findBidLevelIndex(price, found);
                if (found && idx < bids_.size()) {
                    auto& level = bids_[idx];
                    auto& dq = level.orders;

                    // Remove iterator from price level
                    for (auto dit = dq.begin(); dit != dq.end(); ++dit) {
                        if (*dit == ordIt) {
                            level.totalQuantity -= qty;
                            dq.erase(dit);
                            
                            break;
                        }
                    }
                    removeEmptyBidLevel(idx);
                }
            } else {
                bool found = false;
                const std::size_t idx = findAskLevelIndex(price, found);
                if (found && idx < asks_.size()) {
                    auto& level = asks_[idx];
                    auto& dq = level.orders;

                    // Remove iterator from price level
                    for (auto dit = dq.begin(); dit != dq.end(); ++dit) {
                        if (*dit == ordIt) {
                            level.totalQuantity -= qty;
                            dq.erase(dit);
                            break;
                        }
                    }
                    removeEmptyAskLevel(idx);
                }
            }
        }

        // Mark order as inactive
        order.quantity = 0;
        ordersByKey_.erase(it);

        // Returns the order slot to the pool for reuse. The underlying Order object
        // is not erased from orders_ to preserve iterator stability for all resting orders.
        slotPool_.release(ordIt);


    }

    updateTopOfBookAndEmitChanges(oldBid, oldAsk, outputLines);
}

/**
 * Removes all orders and resets the book to an empty state.
 *
 * Rationale:
 *   Used for a global flush. All containers are cleared and sequence
 *   counters reset. Iterator stability is irrelevant after this call.
 */
void SymbolOrderbook::flush() {
    bids_.clear();
    asks_.clear();
    ordersByKey_.clear();
    orders_.clear();
    currentTopBid_ = TopOfBook{};
    currentTopAsk_ = TopOfBook{};
    nextSequence_ = 1;
}

/**
 * Matches a market buy order against the best asks.
 *
 * Rationale:
 *   Market orders execute immediately at the best available price. This
 *   function walks ask levels from lowest price upward, skipping orders
 *   from the same user and updating running totals.
 *
 * Behavior:
 *   - Emits a trade for each partial or full fill.
 *   - Removes filled orders from price levels.
 *   - Stops when the incoming quantity is filled or no asks remain.
 */
void SymbolOrderbook::matchMarketBuy(const Command& cmd,
                                     std::vector<std::string>& outputLines) 
{
    std::int64_t incomingBuyQty = cmd.quantity;
    const int incomingBuyUser = cmd.userId;
    const Order incomingBuyOrder = makeIncomingOrder(cmd);

    while (incomingBuyQty > 0 && !asks_.empty()) 
    {
        // Prefetch next ask level to improve locality when walking levels.
        /*
        if (asks_.size() > 1) {
            __builtin_prefetch(&asks_[1], 0, 1);
        }*/

        auto& askPriceLevel = asks_.front();
        auto& dq = askPriceLevel.orders;

        //Iterate all orders at this price level until we fill incoming buy qty or exhaust all orders at this price level. 
        //Then move to next price level if we still have remaining incoming buy qty.
        for (auto it = dq.begin(); it != dq.end() && incomingBuyQty > 0; ) {

            //Prefetch is not “free.” If loop is already tight and memory‑friendly, prefetching can hurt instead of help.
            //We dont need prefetching here because we are already accessing memory in a linear sequential fashion (perfect for hardware prefetchers)
            //Modern CPUs have aggressive hardware prefetchers that automatically detect this pattern and prefetch the next cache lines without we do anything.
            //Prefetch adds extra branches and iterator work, which can slow down the loop instead of speeding it up.
            //Prefetching the wrong distance hurts, we are prefetching 1 element only. 
            //Prefetching 1 element ahead is too close — the data won’t arrive in time but wasted CPU cycles in prefetching.
            //In short: Our loop is seqential, predictable, branch-tight, cache-friendly. Let hardware prefetcher do its job.
            /*
            if (std::next(it) != dq.end()) {
                OrderIterator nextOrdIt = *std::next(it);
                __builtin_prefetch(&(*nextOrdIt), 0, 1);  // Prefetch next actual Order object
            }*/
            
            //it        → iterator into deque<OrderIterator>
            //*it       → an OrderIterator
            //*(*it)    → an Order (the actual object)          
            OrderIterator ordIt = *it;
            Order& restingAskOrder = *ordIt;

            // Skip self‑matching (same trader)
            if (restingAskOrder.userId == incomingBuyUser) {
                ++it;
                continue;
            }

            // Determine trade quantity
            const std::int64_t tradeQty = std::min(incomingBuyQty, restingAskOrder.quantity);
            incomingBuyQty -= tradeQty;
            restingAskOrder.quantity -= tradeQty;
            askPriceLevel.totalQuantity -= tradeQty;

            emitTrade(incomingBuyOrder, restingAskOrder, restingAskOrder.price, tradeQty, outputLines);

            
            if (restingAskOrder.quantity == 0) {
                ordersByKey_.erase({restingAskOrder.userId, restingAskOrder.userOrderId});

                // Returns the order slot to the pool for reuse. The underlying Order object
                // is not erased from orders_ to preserve iterator stability for all resting orders.
                slotPool_.release(ordIt);


                // Remove filled orders (not actual order but iterator of the order_ element from the price level deque inside asks_ )
                //We are not removing actual order from orders_ because we want to preserve iterator stability for other orders in the same price level.
                it = dq.erase(it);
            } else {
                ++it;
            }
        }//for() loop for this price level orders

        
        if (dq.empty()) //Now we have exhausted all orders at this price level, 
        {
            asks_.erase(asks_.begin()); // Remove empty price level
        } 
        else  // We still have resting orders at this price level, but we have filled all incoming buy qty, so we are done.
        {
            break;
        }

        // If we are here, it means we have exhausted all orders at this price level, 
        //so we move to next price level if we still have remaining incoming buy qty.
    }
}

/**
 * Matches a market sell order against the best bids.
 *
 * Rationale:
 *   Symmetric to matchMarketBuy but operates on the bid side.
 */
void SymbolOrderbook::matchMarketSell(const Command& cmd,
                                      std::vector<std::string>& outputLines) {
    std::int64_t incomingAskQty = cmd.quantity;
    const int incomingAskUser = cmd.userId;
    const Order incomingAskOrder = makeIncomingOrder(cmd);
    
    while (incomingAskQty > 0 && !bids_.empty()) 
    {
        // Prefetch next bid level to improve locality when walking levels.
        /*
        if (bids_.size() > 1) {
            __builtin_prefetch(&bids_[1], 0, 1);
        }*/

        auto& bidPricelevel = bids_.front();
        auto& dq = bidPricelevel.orders;

        for (auto it = dq.begin(); it != dq.end() && incomingAskQty > 0; ) 
        {
            //Commented for reason described earlier in matchMarketBuy()
            /*
            if (std::next(it) != dq.end()) {
                OrderIterator nextOrdIt = *std::next(it);
                __builtin_prefetch(&(*nextOrdIt), 0, 1);  // Prefetch next actual Order object
            } */

            OrderIterator ordIt = *it;
            Order& restingBidOrder = *ordIt;

            // Skip self‑matching
            if (restingBidOrder.userId == incomingAskUser) {
                ++it;
                continue;
            }

            const std::int64_t tradeQty = std::min(incomingAskQty, restingBidOrder.quantity);
            incomingAskQty -= tradeQty;
            restingBidOrder.quantity -= tradeQty;
            bidPricelevel.totalQuantity -= tradeQty;

            emitTrade(restingBidOrder, incomingAskOrder, restingBidOrder.price, tradeQty, outputLines);

            if (restingBidOrder.quantity == 0) {
                ordersByKey_.erase({restingBidOrder.userId, restingBidOrder.userOrderId});

                // Returns the order slot to the pool for reuse. The underlying Order object
                // is not erased from orders_ to preserve iterator stability for all resting orders.
                slotPool_.release(ordIt);

                it = dq.erase(it);
            } else {
                ++it;
            }
        }

        if (dq.empty()) {
            bids_.erase(bids_.begin());
        } else {
            break;
        }
    }
}

/**
 * Matches a limit buy order and rests any remaining quantity.
 *
 * Rationale:
 *   Limit orders match only at prices equal to or better than their limit.
 *   This function walks ask levels from lowest price upward until the
 *   limit price is exceeded or the order is fully matched.
 *
 * Behavior:
 *   - Skips self‑matching.
 *   - Emits trades and updates totals.
 *   - Rests remaining quantity at the appropriate bid level.
 */
void SymbolOrderbook::matchLimitBuy(const Command& cmd,
                                    std::vector<std::string>& outputLines) {
    std::int64_t incomingBuyQty = cmd.quantity;
    const int incomingBuyUser = cmd.userId;
    const std::int64_t incomingLimitBuyPrice = cmd.price;
    const Order incomingBuyOrder = makeIncomingOrder(cmd);
    
    while (incomingBuyQty > 0 && !asks_.empty()) 
    {
        // Prefetch next ask level to improve locality when walking levels.
        /*
        if (asks_.size() > 1) {
            __builtin_prefetch(&asks_[1], 0, 1);
        }*/

        auto& askPriceLevel = asks_.front();
        if (askPriceLevel.price > incomingLimitBuyPrice) {
            break;
        }

        auto& dq = askPriceLevel.orders;
        bool traded = false;
        bool onlySelf = true;

        for (auto it = dq.begin(); it != dq.end() && incomingBuyQty > 0; ) {
            // Prefetch next order iterator to improve locality within the level.
            /*
            if (std::next(it) != dq.end()) {
                OrderIterator nextOrdIt = *std::next(it);
                __builtin_prefetch(&(*nextOrdIt), 0, 1);  // Prefetch next actual Order object
            } */

            OrderIterator ordIt = *it;
            Order& restingAskOrder = *ordIt;

            // Skip self‑matching
            if (restingAskOrder.userId == incomingBuyUser) {
                ++it;
                continue;
            }

            onlySelf = false;

            const std::int64_t tradeQty = std::min(incomingBuyQty, restingAskOrder.quantity);
            incomingBuyQty -= tradeQty;
            restingAskOrder.quantity -= tradeQty;
            askPriceLevel.totalQuantity -= tradeQty;
            traded = true;

            emitTrade(incomingBuyOrder, restingAskOrder, restingAskOrder.price, tradeQty, outputLines);

            if (restingAskOrder.quantity == 0) {
                ordersByKey_.erase({restingAskOrder.userId, restingAskOrder.userOrderId});

                // Returns the order slot to the pool for reuse. The underlying Order object
                // is not erased from orders_ to preserve iterator stability for all resting orders.
                slotPool_.release(ordIt);

                it = dq.erase(it);
            } else {
                ++it;
            }
        }// for() loop for price level orders

        if (dq.empty()) {
            asks_.erase(asks_.begin());
        } else {
            // Not traded at this lowest price level, 
            //means we do not need to check next price level because next price level is even higher, so we are done.
            if (!traded) { 
                break;
            }
            if (onlySelf) {
                break;
            }
        }
    }

    if (incomingBuyQty > 0) {
        
        // The push_back path is no longer used because slotPool_ manages reusable order slots.
        //orders_.push_back(Order{});
        //Order& stored = orders_.back();

        // A new resting order uses a recycled slot from slotPool_ instead of pushing
        // into orders_. This prevents unbounded growth of orders_ and preserves
        // iterator stability for all price‑level references.
        OrderIterator ordIt = slotPool_.allocate();
        Order& stored = *ordIt;

        stored.userId = cmd.userId;
        stored.userOrderId = cmd.userOrderId;
        stored.side = Side::Buy;
        stored.price = cmd.price;
        stored.quantity = incomingBuyQty;
        stored.sequence = nextSequence_++;

        // The push_back path is no longer used because slotPool_ manages reusable
        // order slots. If push_back were used, the iterator would be:
        // OrderIterator ordIt = std::prev(orders_.end());

       
        auto& level = getOrCreateBidLevel(stored.price);
        level.orders.push_back(ordIt);
        level.totalQuantity += stored.quantity;

        ordersByKey_[{stored.userId, stored.userOrderId}] = ordIt;
    }
}

/**
 * Matches a limit sell order and rests any remaining quantity.
 *
 * Rationale:
 *   Symmetric to matchLimitBuy but operates on the bid side. Limit sell
 *   orders match only at prices equal to or higher than their limit.
 */
void SymbolOrderbook::matchLimitSell(const Command& cmd,
                                     std::vector<std::string>& outputLines) {
    std::int64_t incomingAskQty = cmd.quantity;
    const int incomingAskUser = cmd.userId;
    const std::int64_t incomingLimitAskPrice = cmd.price;
    const Order incomingAskOrder = makeIncomingOrder(cmd);

    while (incomingAskQty > 0 && !bids_.empty()) {
        // Prefetch next bid level to improve locality when walking levels.
        /*
        if (bids_.size() > 1) {
            __builtin_prefetch(&bids_[1], 0, 1);
        }*/

        auto& bidPriceLevel = bids_.front();
        if (bidPriceLevel.price < incomingLimitAskPrice) {
            break;
        }

        auto& dq = bidPriceLevel.orders;
        bool traded = false;
        bool onlySelf = true;

        for (auto it = dq.begin(); it != dq.end() && incomingAskQty > 0; ) {
            // Prefetch next order iterator to improve locality within the level.
            /*
            if (std::next(it) != dq.end()) {
                __builtin_prefetch(&(*std::next(it)), 0, 1);
            }*/

            OrderIterator ordIt = *it;
            Order& restingBidOrder = *ordIt;

            // Skip self‑matching
            if (restingBidOrder.userId == incomingAskUser) {
                ++it;
                continue;
            }

            onlySelf = false;

            const std::int64_t tradeQty = std::min(incomingAskQty, restingBidOrder.quantity);
            incomingAskQty -= tradeQty;
            restingBidOrder.quantity -= tradeQty;
            bidPriceLevel.totalQuantity -= tradeQty;
            traded = true;

            emitTrade(restingBidOrder, incomingAskOrder, restingBidOrder.price, tradeQty, outputLines);

            if (restingBidOrder.quantity == 0) {
                ordersByKey_.erase({restingBidOrder.userId, restingBidOrder.userOrderId});

                // Returns the order slot to the pool for reuse. The underlying Order object
                // is not erased from orders_ to preserve iterator stability for all resting orders.
                slotPool_.release(ordIt);

                it = dq.erase(it);
            } else {
                ++it;
            }
        }

        if (dq.empty()) {
            bids_.erase(bids_.begin());
        } else {
            if (!traded) {
                break;
            }
            if (onlySelf) {
                break;
            }
        }
    }

    if (incomingAskQty > 0) {
        // The push_back path is no longer used because slotPool_ manages reusable order slots.
        //orders_.push_back(Order{});
        //Order& stored = orders_.back();

        // A new resting order uses a recycled slot from slotPool_ instead of pushing
        // into orders_. This prevents unbounded growth of orders_ and preserves
        // iterator stability for all price‑level references.
        OrderIterator ordIt = slotPool_.allocate();
        Order& stored = *ordIt;

        stored.userId = cmd.userId;
        stored.userOrderId = cmd.userOrderId;
        stored.side = Side::Sell;
        stored.price = cmd.price;
        stored.quantity = incomingAskQty;
        stored.sequence = nextSequence_++;

        // The push_back path is no longer used because slotPool_ manages reusable
        // order slots. If push_back were used, the iterator would be:
        // OrderIterator ordIt = std::prev(orders_.end());

        auto& level = getOrCreateAskLevel(stored.price);
        level.orders.push_back(ordIt);
        level.totalQuantity += stored.quantity;

        ordersByKey_[{stored.userId, stored.userOrderId}] = ordIt;
    }
}
