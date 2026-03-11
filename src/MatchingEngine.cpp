#include "MatchingEngine.hpp"

/**
 * Processes a single parsed command and appends any output lines.
 *
 * Rationale:
 *   This function is the central dispatcher for all parsed commands.
 *   It interprets the command type and either:
 *     - Applies a global operation (Flush), or
 *     - Routes the command to the appropriate per-symbol order book.
 *
 * Behavior:
 *   - Flush:
 *       Clears the entire books_ map, effectively resetting the engine.
 *   - NewOrder:
 *       Retrieves or creates the SymbolOrderbook for cmd.symbol and
 *       forwards the command to processNewOrder.
 *   - Cancel:
 *       Forwards the cancel command to every existing book because the
 *       symbol is not specified in the cancel format.
 *
 * Complexity:
 *   - Flush: O(S) to destroy S books (amortized by container), where S is the number of symbols.
 *   - NewOrder: O(1) average for map lookup/insert.
 *   - Cancel: O(S) because it iterates over all books, because symbol is not provided.
 */
void MatchingEngine::processCommand(const Command& cmd,
                                    std::vector<std::string>& outputLines) {

    // --------------------------------------------------------------------- 
    // Flush: global reset 
    // ---------------------------------------------------------------------
    if (cmd.type == CommandType::Flush) {
        // All per-symbol books are destroyed; state is fully reset.
        books_.clear();
        return;
    }

    // --------------------------------------------------------------------- 
    // New Order: route to the correct per-symbol book 
    // ---------------------------------------------------------------------
    //Input: N, userId, symbol, price, quantity, side, userOrderId
    if (cmd.type == CommandType::NewOrder) {
        SymbolOrderbook& book = books_[cmd.symbol];
        book.processNewOrder(cmd, outputLines);
        return;
    }

    // --------------------------------------------------------------------- 
    // Cancel: broadcast to all books (symbol not provided) 
    // ---------------------------------------------------------------------
    if (cmd.type == CommandType::Cancel) {
        // Broadcast cancel to all books because symbol is not specified.
        for (auto& entry : books_) {
            entry.second.processCancel(cmd, outputLines);
        }
        return;
    }
}
