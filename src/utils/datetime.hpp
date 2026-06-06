#pragma once
#include <chrono>

namespace Omni {

long get_curr_tstamp_sec();
long get_curr_tstamp_ms();
long get_curr_tstamp_ns();

long get_curr_intraday_minute(long timezone_minute_offset);
long get_curr_date(long timezone_minute_offset);

} // namespace Omni
