#include <map>
#include <mutex>
#include <optional>
#include <sndfile.h>
#include <string>
#include <vector>

#include "frequencies.h"
#include "sample.h"


float find_loudest_frequency(const std::vector<std::vector<float> > & samples, const unsigned sample_sample_rate)
{
	size_t  n_ch      = samples.at(0).size();
	size_t  n_samples = samples.size();
	double *mono      = new double[n_samples]();
	for(size_t i=0; i<n_samples; i++) {
		for(size_t ch=0; ch<n_ch; ch++)
			mono[i] += samples[i][ch];
		mono[i] /= n_ch;
	}

	float loudest_frequency = find_loudest_freq(mono, n_samples, sample_sample_rate);
	delete [] mono;
	return loudest_frequency;
}

std::pair<std::optional<sample_t>, std::string> load_sample(const std::string & filename)
{
	SF_INFO si { };
	SNDFILE *sh = sf_open(filename.c_str(), SFM_READ, &si);
	if (!sh)
		return { { }, sf_strerror(sh) };

	std::vector<std::vector<float> > samples;

	constexpr int load_buffer_size = 65536;
	float *buffer = new float[load_buffer_size * si.channels];

	for(;;) {
		sf_count_t cur_n = sf_readf_float(sh, buffer, load_buffer_size);
		if (cur_n == 0)
			break;

		for(sf_count_t i=0; i<cur_n; i++) {
			int offset = i * si.channels;
			std::vector<float> row;
			for(int j=offset; j<offset + si.channels; j++)
				row.push_back(buffer[j]);
			samples.push_back(row);
		}
	}

	sf_close(sh);
	delete [] buffer;

	float loudest_frequency = find_loudest_frequency(samples, si.samplerate);
	printf("Loudest frequency of \"%s\": %.1f Hz\n", filename.c_str(), loudest_frequency);

	sample_t out { std::move(samples), si.samplerate, loudest_frequency };

	return { out, "" };
}
