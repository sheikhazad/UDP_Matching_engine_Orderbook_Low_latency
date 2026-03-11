#pragma once

#include <cstddef>
#include <functional>

/// Unique key for an order based on userId and userOrderId.
struct OrderKey {
    int userId{0};
    int userOrderId{0};

    /// Compare two keys for equality.
    bool operator==(const OrderKey& other) const noexcept {
        return userId == other.userId && userOrderId == other.userOrderId;
    }
};

/// Hash function for OrderKey to use in unordered_map.
struct OrderKeyHash {
    std::size_t operator()(const OrderKey& k) const noexcept {
        //shift h2 (hash of userOrderId) to occupy a different bit pattern than hash(h1) of userId.
        //It helps avoid simple collisions like (1 ^ 2) vs (2 ^ 1).
        //(1 ^ 2) and (2 ^ 1) will produce same hash value without shifting.
        return std::hash<int>()(k.userId) ^ (std::hash<int>()(k.userOrderId) << 1);
    }
};
