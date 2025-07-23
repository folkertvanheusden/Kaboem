#include <fftw3.h>
#include <string>


class fft
{
private:
	const double *pin;
	fftw_complex *pout;
	fftw_plan     plan;
	int           n_samples_in;

public:
	fft(const int n_samples_in, const double *const data_in);
	~fft();

	void do_fft(double *o);
};

double      find_loudest_freq(const double *const in, const size_t n, const int sample_rate);
int         frequency_to_midi_note(const double f);
double      midi_note_to_frequency(const int note);
std::string midi_note_to_name(int note);
