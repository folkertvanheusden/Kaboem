#include <cmath>
#include <cstring>

#include "filter.h"


filter_butterworth::filter_butterworth(const int sample_rate, const double is_high_pass, const double resonance):
	sample_rate(sample_rate),
	is_high_pass(is_high_pass),
	resonance(resonance)
{
}

void filter_butterworth::configure(const double frequency)
{
	if (is_high_pass) {
		double c = tan(M_PI * frequency / sample_rate);
		a1 = 1.0 / (1.0 + resonance * c + c * c);
		a2 = -2.0 * a1;
		a3 = a1;
		b1 = 2.0 * (c * c - 1.0) * a1;
		b2 = (1.0 - resonance * c + c * c) * a1;
	}
	else {
		double c = 1.0 / tan(M_PI * frequency / sample_rate);
		a1 = 1.0 / (1.0 + resonance * c + c * c);
		a2 = 2.0 * a1;
		a3 = a1;
		b1 = 2.0 * (1.0 - c * c) * a1;
		b2 = (1.0 - resonance * c + c * c) * a1;
	}
}

double filter_butterworth::apply(const double new_input)
{
	double new_output = a1 * new_input + a2 * input_history[0] + a3 * input_history[1] - b1 * output_history[0] - b2 * output_history[1];

	input_history[1]  = input_history[0];
	input_history[0]  = new_input;

	output_history[2] = output_history[1];
	output_history[1] = output_history[0];
	output_history[0] = new_output;

	return output_history[0];
}
