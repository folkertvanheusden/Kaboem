#include <cstdint>
#include <ctime>


uint64_t get_ms()
{
	timespec ts { };
	clock_gettime(CLOCK_REALTIME, &ts);
	return uint64_t(ts.tv_sec) * uint64_t(1000) + uint64_t(ts.tv_nsec / 1000000);
}

uint64_t get_us()
{
	timespec ts { };
	clock_gettime(CLOCK_REALTIME, &ts);
	return uint64_t(ts.tv_sec) * uint64_t(1000000) + uint64_t(ts.tv_nsec / 1000);
}
