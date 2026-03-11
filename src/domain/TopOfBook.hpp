#pragma once

#include <cstdint>

/// Snapshot of top-of-book for one side.
struct TopOfBook {
    bool valid{false};
    std::int64_t price{0};
    std::int64_t totalQuantity{0};
};
