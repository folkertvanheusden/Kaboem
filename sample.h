#include <optional>
#include <string>
#include <vector>


// data, sample rate, (loudest-) frequency
std::optional<std::tuple<std::vector<std::vector<float> > *, unsigned int, float> > load_sample(const std::string & filename);
float find_loudest_frequency(const std::vector<std::vector<float> > & samples, const unsigned sample_sample_rate);
