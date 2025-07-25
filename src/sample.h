#include <optional>
#include <string>
#include <vector>


struct sample_t {
	std::vector<std::vector<float> > samples;
	int                              sample_rate;
	float                            loudest_frequency;
};

std::pair<std::optional<sample_t>, std::string> load_sample(const std::string & filename);
float find_loudest_frequency(const std::vector<std::vector<float> > & samples, const unsigned sample_sample_rate);
