#include <cfloat>
#include <cmath>

#include "audio.h"
#include "frequencies.h"
#include "sample.h"
#include "sound.h"
#include "time.h"


double f_to_delta_t(const double frequency, const int sample_rate)
{
	return 2 * M_PI * frequency / sample_rate;
}

sound_sample::sound_sample(const int sample_rate, const std::string & file_name) :
	sound(sample_rate, sample_rate / 2),
	file_name(file_name)
{
}

sound_sample::sound_sample(const int sample_rate, const std::string & file_name, const std::vector<std::vector<float> > & sample_data, const unsigned sample_sample_rate) :
	sound(sample_rate, sample_rate / 2),
	file_name(file_name),
	samples(sample_data),
	sample_sample_rate(sample_sample_rate)
{
}

bool sound_sample::begin()
{
	if (samples.empty() == true) {
		auto            rc = load_sample(file_name);
		if (rc.has_value() == false) {
			printf("Cannot access sample \"%s\" in cache\n", file_name.c_str());
			return false;
		}
		samples            = *std::get<0>(rc.value());
		sample_sample_rate =  std::get<1>(rc.value());
		base_frequency     =  ceil(std::get<2>(rc.value()));
	}
	else {
		base_frequency     = find_loudest_frequency(samples, sample_sample_rate);
	}

	base_midi_note     = frequency_to_midi_note(base_frequency);
	name               = midi_note_to_name(base_midi_note);
	delta_t            = sample_sample_rate / double(sample_rate);

	input_output_matrix.resize(samples.at(0).size());

	printf("Sample %s has %zu channel(s), is sampled at %u Hz and sounds like a %s (%.2f Hz), duration: %.2fs\n", file_name.c_str(), input_output_matrix.size(), sample_sample_rate, name.c_str(), base_frequency, samples.size() / double(sample_sample_rate));

	return true;
}

std::string sound_sample::get_name() const
{
	return name;
}

std::optional<std::pair<float, std::map<int, float> > > sound_sample::get_sample(const double t, const size_t channel_nr) const
{
	double use_t = t * delta_t * pitchbend;
	if (use_t < 0 || use_t >= samples.size())
		return { };

	size_t offset = use_t;

	return { { samples.at(offset).at(channel_nr), input_output_matrix[channel_nr] } };
}
