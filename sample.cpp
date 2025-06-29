#include <map>
#include <mutex>
#include <optional>
#include <sndfile.h>
#include <string>
#include <vector>

#include "error.h"
#include "frequencies.h"


static std::mutex sample_cache_lock;
static std::map<std::string, std::tuple<std::vector<std::vector<double> > *, unsigned int, double> > sample_cache;

double find_loudest_frequency(const std::vector<std::vector<double> > & samples, const unsigned sample_sample_rate)
{
	size_t  n_ch      = samples.at(0).size();
	size_t  n_samples = samples.size();
	double *mono      = new double[n_samples]();
	for(size_t i=0; i<n_samples; i++) {
		for(size_t ch=0; ch<n_ch; ch++)
			mono[i] += samples[i][ch];
		mono[i] /= n_ch;
	}

	double loudest_frequency = find_loudest_freq(mono, n_samples, sample_sample_rate);
	delete [] mono;
	return loudest_frequency;
}

std::optional<std::tuple<std::vector<std::vector<double> > *, unsigned int, double> > load_sample(const std::string & filename)
{
	std::unique_lock<std::mutex> lck(sample_cache_lock);

	auto it = sample_cache.find(filename);
	if (it != sample_cache.end())
		return it->second;

        SF_INFO si = { 0 };
        SNDFILE *sh = sf_open(filename.c_str(), SFM_READ, &si);
	if (!sh)
		return { };

	auto *samples = new std::vector<std::vector<double> >();

	constexpr int load_buffer_size = 4096;
	double *buffer = new double[load_buffer_size * si.channels];

	for(;;) {
		sf_count_t cur_n = sf_readf_double(sh, buffer, load_buffer_size);
		if (cur_n == 0)
			break;

		for(sf_count_t i=0; i<cur_n; i++) {
			int offset = i * si.channels;

			std::vector<double> row;
			for(int j=offset; j<offset + si.channels; j++)
				row.push_back(buffer[j]);
			samples->push_back(row);
		}
	}

	sf_close(sh);
	delete [] buffer;

	double loudest_frequency = find_loudest_frequency(*samples, si.samplerate);
	printf("loudest_frequency of \"%s\": %.1f\n", filename.c_str(), loudest_frequency);

	sample_cache.insert({ filename, { samples, si.samplerate, loudest_frequency } });

	return { { samples, si.samplerate, loudest_frequency } };
}

void unload_sample_cache()
{
	std::unique_lock<std::mutex> lck(sample_cache_lock);
	for(auto & entry : sample_cache)
		delete std::get<0>(entry.second);
	sample_cache.clear();
}
