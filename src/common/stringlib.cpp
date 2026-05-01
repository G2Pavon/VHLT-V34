#include "stringlib.h"

#include <algorithm>
#include <cctype>

bool str_casecmp(std::string_view s1, std::string_view s2)
{
    if (s1.size() != s2.size())
        return false;

    return std::equal(s1.begin(), s1.end(), s2.begin(),
                      [](unsigned char c1, unsigned char c2)
                      {
                          return std::tolower(c1) == std::tolower(c2);
                      });
}

bool str_ncasecmp(std::string_view s1, std::string_view s2, size_t n)
{
    auto sub1 = s1.substr(0, std::min(s1.size(), n));
    auto sub2 = s2.substr(0, std::min(s2.size(), n));

    if (sub1.size() != sub2.size())
        return false;

    return std::equal(sub1.begin(), sub1.end(), sub2.begin(),
                      [](unsigned char c1, unsigned char c2)
                      {
                          return std::tolower(c1) == std::tolower(c2);
                      });
}