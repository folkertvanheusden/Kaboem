#include <cstdint>


void midi_processor(sound_parameters *const sound_pars, const std::vector<uint8_t> & msg)
{
	// assuming any channel is for kaboem, main already filters out percussion

	if (msg.size() < 2)
		return;

	uint8_t cmd     = msg.at(0) & 0xf0;
	uint8_t channel = msg.at(0) & 0x0f;

	if (cmd == 0x90) {
	}
}
