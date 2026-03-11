#pragma once

#include <cstdint>
#include "types.hpp"

/// Internal representation of an order stored in the book.
struct Order {
    int userId{0};
    int userOrderId{0};
    Side side{Side::Buy};
    std::int64_t price{0};
    std::int64_t quantity{0};      // remaining quantity
    std::uint64_t sequence{0};     // time priority within the book
};
