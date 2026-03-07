#include <atomic>
#include <cstdint>

extern std::atomic_uint64_t my_clock;

void start_clock();
void stop_clock();
bool is_clock_ticking();
void reset_clock();
