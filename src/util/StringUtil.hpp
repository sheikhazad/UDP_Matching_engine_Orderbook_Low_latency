#pragma once

#include <string>
#include <vector>

/// Trim leading and trailing whitespace from a string in place.
void trim(std::string& s);

/// Split a CSV line into tokens, trimming whitespace around each token.
std::vector<std::string> split_csv(const std::string& line);
