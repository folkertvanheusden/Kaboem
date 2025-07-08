#include <cmath>

#include "agc.h"


agc::agc(const float threshold_db, const float ratio, const float attack_ms, const float release_ms, const int sample_rate):
	threshold_db(threshold_db),
	ratio(ratio),
	attack_coefficient (1.0f - std::exp(-1.0f / (0.001f * attack_ms  * sample_rate))),
	release_coefficient(1.0f - std::exp(-1.0f / (0.001f * release_ms * sample_rate))),
	envelope(0.0f)
{
}

float agc::calculate_gain(const float input)
{
	float abs_input = fabsf(input);
	float db_input  = 20.0f * log10f(abs_input + 1e-8f);  // avoid log(0)

	// Envelope follower (peak detection with attack/release smoothing)
	float coeff = db_input > envelope ? attack_coefficient : release_coefficient;
	envelope = (1.0f - coeff) * envelope + coeff * db_input;

	// Compute gain reduction
	float gain_db = 0.0f;
	if (envelope > threshold_db)
		gain_db = (threshold_db - envelope) * (1.0f - 1.0f / ratio);

	// Convert gain to linear
	float gain_linear = std::powf(10.0f, gain_db / 20.0f);
	return gain_linear;
}
