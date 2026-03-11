#include "domain/commandParser.hpp"

#include <string>
#include <vector>
#include "util/StringUtil.hpp"

/// Parse a single CSV line into a Command structure.
//OrderType=> Expected input formats:
//NewOrder => N, userId, symbol, price, quantity, side, userOrderId
//Cancel   => C, userId, userOrderId
//Flush    => F

Command parseLineToCommand(const std::string& line) {
    Command cmd;
    const auto tokens = split_csv(line);

    if (tokens.empty()) {
        cmd.type = CommandType::Invalid;
        return cmd;
    }

    const std::string& tag = tokens[0];

    if (tag == "F") {
        cmd.type = CommandType::Flush;
        return cmd;
    }

    if (tag == "N") {
        if (tokens.size() != 7) {
            cmd.type = CommandType::Invalid;
            return cmd;
        }

        cmd.type = CommandType::NewOrder;
        cmd.userId = std::stoi(tokens[1]);
        cmd.symbol = tokens[2];
        cmd.price = std::stoll(tokens[3]);
        cmd.quantity = std::stoll(tokens[4]);
        cmd.side = (tokens[5] == "S") ? Side::Sell : Side::Buy;
        cmd.userOrderId = std::stoi(tokens[6]);
        return cmd;
    }

    if (tag == "C") {
        if (tokens.size() != 3) {
            cmd.type = CommandType::Invalid;
            return cmd;
        }

        cmd.type = CommandType::Cancel;
        cmd.userId = std::stoi(tokens[1]);
        cmd.userOrderId = std::stoi(tokens[2]);
        return cmd;
    }

    cmd.type = CommandType::Invalid;
    return cmd;
}
