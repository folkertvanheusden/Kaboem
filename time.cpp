#include <chrono>
#include <cstdint>


uint64_t get_ms()
{
	auto now = std::chrono::steady_clock::now();
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	return static_cast<uint64_t>(millis);
}

uint64_t get_us()
{
	auto now = std::chrono::steady_clock::now();
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
	return static_cast<uint64_t>(micros);
}
