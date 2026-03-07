#include <atomic>
#include <cstdint>
#include <SDL3/SDL_timer.h>


std::atomic_uint64_t my_clock { 0 };

constexpr const Uint64   interval  = 1000000;  // 1 ms
constexpr const uint64_t clock_add = 1000;  // 1000 us in 1 ms
static SDL_TimerID clock_handle  {       };
static bool        clock_ticking { false };

static Uint64 ticker(void *userdata, SDL_TimerID timerID, Uint64 interval)
{
	my_clock += clock_add;
	return interval;
}

void start_clock()
{
	clock_handle  = SDL_AddTimerNS(interval, ticker, nullptr);
	clock_ticking = true;
}

void stop_clock()
{
	SDL_RemoveTimer(clock_handle);
	clock_ticking = false;
}

bool is_clock_ticking()
{
	return clock_ticking;
}

void reset_clock()
{
	my_clock = 0;
}
