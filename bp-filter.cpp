#include <cmath>

#include "bp-filter.h"


biquad_filter::biquad_filter(const float b0, const float b1, const float b2, const float a1, const float a2)
	: b0(b0), b1(b1), b2(b2), a1(a1), a2(a2),
	x1(0.0), x2(0.0), y1(0.0), y2(0.0)
{
}

float biquad_filter::process(float input)
{
	float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

	x2 = x1;
	x1 = input;
	y2 = y1;
	y1 = output;

	return output;
}

biquad_filter *design_bandpass(const float sample_rate, const float low_freq, const float high_freq)
{
	float f0 = sqrt(low_freq * high_freq);
	float BW = high_freq - low_freq;
	float Q = f0 / BW;

	float omega = 2.0 * M_PI * f0 / sample_rate;
	float alpha = sin(omega) / (2.0 * Q);
	float cos_omega = cos(omega);

	float b0 = alpha;
	float b1 = 0.0;
	float b2 = -alpha;
	float a0 = 1.0 + alpha;
	float a1 = -2.0 * cos_omega;
	float a2 = 1.0 - alpha;

	b0 /= a0;
	b1 /= a0;
	b2 /= a0;
	a1 /= a0;
	a2 /= a0;

	return new biquad_filter(b0, b1, b2, a1, a2);
}
