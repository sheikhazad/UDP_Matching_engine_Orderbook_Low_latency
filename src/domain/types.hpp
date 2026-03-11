#pragma once

#include <cstdint>
#include <string>

/// Side of an order: buy or sell.
enum class Side {
    Buy,
    Sell
};

/// Type of incoming command.
enum class CommandType {
    NewOrder, //Input: N, userId, symbol, price, quantity, side, userOrderId
    Cancel,   //Input: C, userId, userOrderId
    Flush,    //Input: F
    Invalid
};

/// Parsed representation of an incoming command line.
struct Command {
    CommandType type{CommandType::Invalid};
    int userId{0};
    std::string symbol;
    std::int64_t price{0};
    std::int64_t quantity{0};
    Side side{Side::Buy};
    int userOrderId{0};  // unique order id per user
};
