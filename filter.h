#pragma once

// based on http://stackoverflow.com/questions/8079526/lowpass-and-high-pass-filter-in-c-sharp
class filter_butterworth
{
private:
	const int    sample_rate  { 44100 };
	const bool   is_high_pass { false };
	const double resonance    { 1.    };

	/// <summary>
	/// rez amount, from sqrt(2) to ~ 0.1
	/// </summary>
	/// resonance;

	//
	double a1 { };
	double a2 { };
	double a3 { };
	double b1 { };
	double b2 { };

	/// <summary>
	/// Array of input values, latest are in front
	/// </summary>
	double input_history[2] { };

	/// <summary>
	/// Array of output values, latest are in front
	/// </summary>
	double output_history[3] { };

public:
	filter_butterworth(const int sample_rate, const double is_high_pass, const double resonance);

	void   configure(const double frequency);
	double apply    (const double new_input);
};
