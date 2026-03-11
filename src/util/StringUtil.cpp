#include "util/StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

/// Trim leading and trailing whitespace from a string in place.
void trim(std::string& s) 
{

    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    const auto itFront = std::find_if(s.begin(), s.end(), notSpace);
    //.base() gives the corresponding forward iterator & point to one past the last non-space
    const auto itBack = std::find_if(s.rbegin(), s.rend(), notSpace).base(); 

    if (itFront >= itBack) {
        s.clear();
    } else {
        s.assign(itFront, itBack);
    }
}

/// Split a CSV line into tokens, trimming whitespace around each token.
std::vector<std::string> split_csv(const std::string& line) 
{
    std::vector<std::string> tokens;
    std::string aToken;
    std::istringstream iss(line);

    while (std::getline(iss, aToken, ',')) {
        trim(aToken);
        tokens.push_back(aToken);
    }

    //NRVO: compiler will construct tokens directly in the caller’s storage.
    //If NRVO fails, move constructor will be used to avoid copy.
    return tokens;
}
