#pragma once

#include <string>
#include "domain/types.hpp"

/// Parse a single CSV line into a Command structure.
/// Returns a Command with type == CommandType::Invalid on parse failure.
Command parseLineToCommand(const std::string& line);
