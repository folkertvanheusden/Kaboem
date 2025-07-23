class biquad_filter {
private:
	const float b0, b1, b2, a1, a2;
	      float x1, x2, y1, y2;

public:
	biquad_filter(const float b0, const float b1, const float b2, const float a1, const float a2);
	float process(const float input);
};

biquad_filter *design_bandpass(const float sample_rate, const float low_freq, const float high_freq);
