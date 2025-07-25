#include <cstdint>
#include <string>
#include <vector>

#include "sound.h"


typedef struct
{
	std::string  file_name;
	double       base_freq;
	int          key;
	unsigned int sample_rate;

	size_t       repeat_start[2];
	size_t       repeat_end  [2];

	std::vector<std::vector<float> > samples;
} sf2_sample_t;

typedef struct
{
	std::vector<uint8_t> channels, instruments;
} filter_t;

typedef struct
{
	std::string           name;
	std::vector<sf2_sample_t> samples;
	// map of midi-note to index in samples-vector
	ssize_t               sample_map[128];
	filter_t              filter;
} sample_set_t;

std::map<uint16_t, sample_set_t> load_sf2(const std::string & file_name, const bool isPercussion);
sample                           convert_sf2_sample(sf2_sample_t *const in);
