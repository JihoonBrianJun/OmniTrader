#include <ctime>
#include "datetime.hpp"

namespace Omni {

long get_curr_tstamp_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

long get_curr_tstamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

long get_curr_tstamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

long get_curr_intraday_minute(long timezone_minute_offset) {
    return (get_curr_tstamp_sec() / 60 + timezone_minute_offset) % 1440;
}

long get_curr_date(long timezone_minute_offset) {
    // Get current timestamp and adjust for timezone
    long adjusted_timestamp = get_curr_tstamp_sec() + (timezone_minute_offset * 60);

    // Convert to time structure
    std::time_t time = static_cast<std::time_t>(adjusted_timestamp);
    std::tm* tm_info = std::gmtime(&time);

    // Format as YYYYMMDD
    long year = tm_info->tm_year + 1900;
    long month = tm_info->tm_mon + 1;
    long day = tm_info->tm_mday;

    return year * 10000 + month * 100 + day;
}

} // namespace Omni
