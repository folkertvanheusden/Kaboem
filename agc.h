class agc {
private:
	const double threshold_db         { 0. };
	const double ratio                { 0. };
	const double attack_coefficient   { 0. };
	const double release_coefficient  { 0. };
	const int    sample_rate          { 0 };
	      double envelope             { 0. };

public:
	agc(const double threshold_db, const double ratio, const double attack_ms, const double release_ms, const int sample_rate);
	double calculate_gain(const double input);
};
