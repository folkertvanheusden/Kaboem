#include <cmath>
#include <cstring>
#include <fftw3.h>
#include <string>

#include "frequencies.h"


fft::fft(const int n_samples_in_in, const double *const data)
{
	n_samples_in = n_samples_in_in;
	pin          = data;
	pout         = reinterpret_cast<fftw_complex *>(fftw_malloc(sizeof(fftw_complex) * n_samples_in + 1));
	plan         = fftw_plan_dft_r2c_1d(n_samples_in, const_cast<double *>(pin), pout, FFTW_ESTIMATE);
}

fft::~fft()
{
	fftw_free(pout);
}

void fft::do_fft(double *o)
{
	fftw_execute(plan);

	for(int loop=0; loop<(n_samples_in / 2) + 1; loop++)
	{
		double real = pout[loop][0];
		double img  = pout[loop][1];
		double mag  = hypot(real, img);
		o[loop] = mag;
	}
}

static void do_fft(const double *const in, const size_t n, double **out, size_t *n_out)
{
	double *temp = new double[n];
	memcpy(temp, in, n * sizeof(double));

	fft f(n, temp);

	size_t hn = n / 2 + 1;

	*out   = new double[hn];
	*n_out = hn;

	f.do_fft(*out);

	delete [] temp;
}

double find_loudest_freq(const double *const in, const size_t n, const int sample_rate)
{
	double *fd  = NULL;
	size_t  nfd = 0;
	do_fft(in, n, &fd, &nfd);

	double maxval = -1;
	size_t bin    =  0;
	for(size_t i=0; i<nfd; i++) {
		if (fd[i] > maxval) {
			maxval = fd[i];
			bin = i;
		}
	}

	delete [] fd;

	double bin_size    = double(n) / sample_rate;
	double freq_of_bin = bin / bin_size;

	return freq_of_bin;
}

constexpr const double base_note = 440.;

int frequency_to_midi_note(const double f)
{
	return 69 + 12 * log2(f / base_note);
}

double midi_note_to_frequency(const int note)
{
	return base_note * pow(2., (note - 69) / 12.);
}

std::string midi_note_to_name(int note)
{
    constexpr const char *const note_names[] { "A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#" };
    note -= 21;  // A0
    int octave = note / 12 + 1;
    while(note < 0)
	    note += 12;
    std::string out = note_names[note % 12];
    if (octave)
	    out += std::to_string(octave);
    return out;
}
