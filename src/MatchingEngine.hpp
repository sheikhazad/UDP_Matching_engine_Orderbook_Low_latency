#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "domain/types.hpp"
#include "SymbolOrderbook.hpp"

/**
 * MatchingEngine owns all per-symbol order books and routes commands.
 *
 * Threading model:
 *   - MatchingEngine is intended to be used from a single processing thread.
 *   - All access to order book state is serialized through processCommand().
 *
 * Responsibilities:
 *   - Lazily create SymbolOrderbook instances per symbol.
 *   - Route NewOrder commands to the correct book.
 *   - Broadcast Cancel commands to all books (symbol not provided).
 *   - Apply global operations such as Flush.
 *
 * Complexity:
 *   - NewOrder: average O(1) routing by symbol.
 *   - Cancel: O(S) over symbols, each book may be O(log P) or O(1) internally.
 *   - Flush: O(S) to clear all books.
 */
class MatchingEngine {
public:
    /**
     * Processes a single parsed command and appends any output lines.
     *
     * Rationale:
     *   This is the main entry point for the engine. It interprets the
     *   command type and routes it to the correct per-symbol book or
     *   applies a global operation such as flush.
     *
     * Behavior:
     *   - Flush: clears all books.
     *   - NewOrder: routes to the book for the given symbol, creating it if needed.
     *   - Cancel: broadcasts to all books because symbol is not provided.
     */
    void processCommand(const Command& cmd,
                        std::vector<std::string>& outputLines);

private:
    // Per-symbol order books, created lazily on first use.
    std::unordered_map<std::string, SymbolOrderbook> books_;
};
