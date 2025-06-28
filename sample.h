#include <optional>
#include <string>
#include <vector>


// data, sample rate, (loudest-) frequency
std::optional<std::tuple<std::vector<std::vector<double> > *, unsigned int, double> > load_sample(const std::string & filename);
void unload_sample_cache();
