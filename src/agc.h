class agc {
private:
	const float threshold_db         { 0. };
	const float ratio                { 0. };
	const float attack_coefficient   { 0. };
	const float release_coefficient  { 0. };
	      float envelope             { 0. };

public:
	agc(const float threshold_db, const float ratio, const float attack_ms, const float release_ms, const int sample_rate);
	float calculate_gain(const float input);
};
