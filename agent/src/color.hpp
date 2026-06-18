// Terminal color helpers
#pragma once

#include <cstdio>
#include <unistd.h>

namespace eulerpilot { namespace color {

inline constexpr const char* reset   = "\033[0m";
inline constexpr const char* bold    = "\033[1m";
inline constexpr const char* dim     = "\033[2m";
inline constexpr const char* red     = "\033[31m";
inline constexpr const char* green   = "\033[32m";
inline constexpr const char* yellow  = "\033[33m";
inline constexpr const char* blue    = "\033[34m";
inline constexpr const char* magenta = "\033[35m";
inline constexpr const char* cyan    = "\033[36m";

inline bool is_tty() {
    static bool tty = isatty(fileno(stdout));
    return tty;
}

inline const char* r()    { return is_tty() ? reset : ""; }
inline const char* b()    { return is_tty() ? bold : ""; }
inline const char* dim_() { return is_tty() ? dim : ""; }
inline const char* red_()    { return is_tty() ? red : ""; }
inline const char* green_()  { return is_tty() ? green : ""; }
inline const char* yellow_() { return is_tty() ? yellow : ""; }
inline const char* cyan_()   { return is_tty() ? cyan : ""; }
inline const char* magenta_(){ return is_tty() ? magenta : ""; }
inline const char* blue_()   { return is_tty() ? blue : ""; }

}} // namespace eulerpilot::color
