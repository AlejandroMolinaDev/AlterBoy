#pragma once

// Tiny check helper for the host probe (prints PASS/FAIL lines, counts failures).

#include <iostream>
#include <string>

namespace probe
{
inline int& failures()
{
    static int count = 0; // single-threaded test tool, not plugin code
    return count;
}

inline bool check (bool condition, const std::string& what)
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! condition)
        ++failures();
    return condition;
}

int runVst3Probe (const std::string& path);
int runClapProbe (const std::string& path);
} // namespace probe
