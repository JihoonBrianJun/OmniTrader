#pragma once

#include <string>

namespace Omni {

using TickFunc = double (*)(double);

TickFunc get_tick_func(const std::string& exchange);
double get_min_tick(const std::string& exchange);

} // namespace Omni
