#include <cfloat>
#include <cmath>

#include "frequencies.h"
#include "pipewire-audio.h"
#include "sample.h"
#include "sound.h"
#include "time.h"


double f_to_delta_t(const double frequency, const int sample_rate)
{
	return 2 * M_PI * frequency / sample_rate;
}

void on_process_audio(void *userdata)
{
	uint64_t          t  = get_us();
	sound_parameters *sp = reinterpret_cast<sound_parameters *>(userdata);
	pw_buffer        *b  = pw_stream_dequeue_buffer(sp->pw.stream);
	if (b == nullptr) {
		pw_log_warn("out of buffers: %m");
		return;
	}
	spa_buffer *buf      = b->buffer;

	int     stride       = sizeof(double) * sp->n_channels;
	// 75: audio-CD had chunks of 1/75th of a second. this gives a latency of around 13.1 ms
	int     period_size  = std::min(buf->datas[0].maxsize / stride, uint32_t(sp->sample_rate / 75));
	double  latency      = period_size * 1000000.0 / sp->sample_rate;

	double *dest         = reinterpret_cast<double *>(buf->datas[0].data);
	if (!dest) {
		printf("no buffer\n");
		return;
	}

	double *temp_buffer  = new double[sp->n_channels * period_size]();

	std::shared_lock<std::shared_mutex> lck(sp->sounds_lock);

	for(int t=0; t<period_size; t++) {
		double *current_sample_base = &temp_buffer[t * sp->n_channels];

		for(size_t s_idx=0; s_idx<sp->sounds.size();) {
			auto & item = sp->sounds[s_idx];
			if (item.s) {
				if (item.s->set_time(item.t * item.pitch)) {
					sp->sounds.erase(sp->sounds.begin() + s_idx);
					continue;
				}

				if (item.s->get_mute() == false) {
					size_t n_source_channels = item.s->get_n_channels();

					for(size_t ch=0; ch<n_source_channels; ch++) {
						auto   rc    = item.s->get_sample(ch);
						double value = rc.first;

						for(auto mapping : rc.second)
							current_sample_base[mapping.first] += value * mapping.second;
					}
				}
			}

			item.t++;
			s_idx++;
		}
	}

	sp->n_loud_checked += period_size;

	if (sp->agc_enabled) {
		double *c_temp = new double[sp->n_channels];
		for(int t=0; t<period_size; t++) {
			double *current_sample_base_in  = &temp_buffer[t * sp->n_channels];
			double *current_sample_base_out = &dest[t * sp->n_channels];

			double gain = DBL_MAX;
			for(int c=0; c<sp->n_channels; c++) {
				c_temp[c] = current_sample_base_in[c] * sp->global_volume;
				gain      = std::min(gain, sp->agc_instances[c]->calculate_gain(c_temp[c]));
			}

			for(int c=0; c<sp->n_channels; c++) {
				double temp = std::clamp(c_temp[c] * gain, -1., 1.);

				if (sp->filter_lp)
					temp = sp->filter_lp->apply(temp);
				if (sp->filter_hp)
					temp = sp->filter_hp->apply(temp);

				double sign = temp < 0 ? -1 : 1;
				current_sample_base_out[c] = pow(fabs(temp), sp->sound_saturation) * sign;
			}
		}
		delete [] c_temp;
	}
	else {
		for(int t=0; t<period_size; t++) {
			double *current_sample_base_in  = &temp_buffer[t * sp->n_channels];
			double *current_sample_base_out = &dest[t * sp->n_channels];

			double too_loud = 0;
			for(int c=0; c<sp->n_channels; c++) {
				double temp = current_sample_base_in[c] * sp->global_volume;

				if (temp < -1.)
					temp = -1., too_loud = std::max(too_loud, fabs(temp));
				else if (temp > 1.)
					temp = 1.,  too_loud = std::max(too_loud, temp);

				if (sp->filter_lp)
					temp = sp->filter_lp->apply(temp);
				if (sp->filter_hp)
					temp = sp->filter_hp->apply(temp);

				double sign = temp < 0 ? -1 : 1;
				current_sample_base_out[c] = pow(fabs(temp), sp->sound_saturation) * sign;
			}

			sp->too_loud_total += too_loud;
			sp->too_loud_count++;
		}
	}

	delete [] temp_buffer;

	buf->datas[0].chunk->offset = 0;
	buf->datas[0].chunk->stride = stride;
	buf->datas[0].chunk->size   = period_size * stride;
	if (pw_stream_queue_buffer(sp->pw.stream, b))
		printf("pw_stream_queue_buffer failed\n");

	if (sp->record_handle) 
		sf_writef_double(sp->record_handle, dest, period_size);

	// scope
	sp->scope.clear();
	sp->scope.resize(period_size);

	for(int i=0; i<period_size; i++) {
		for(int c=0; c<sp->n_channels; c++)
			sp->scope[i] += dest[i * sp->n_channels + c];

		sp->scope[i] /= sp->n_channels;
	}

	sp->scope_t++;

	// statistics
	sp->n_busyness++;
	sp->t_busyness += 100 * (get_us() - t) / latency;

	if (sp->n_loud_checked >= sp->sample_rate / 2) {
		if (sp->too_loud_count > 0)
			sp->clip_factor = sp->too_loud_total / sp->too_loud_count;
		else
			sp->clip_factor = 0;
		sp->too_loud_total = 0;
		sp->too_loud_count = 0;
		sp->n_loud_checked = 0;
		sp->busyness       = sp->t_busyness / sp->n_busyness;
		sp->n_busyness     = 0;
		sp->t_busyness     = 0;
	}
}

sound_sample::sound_sample(const int sample_rate, const std::string & file_name) :
	sound(sample_rate, sample_rate / 2),
	file_name(file_name)
{
}

sound_sample::sound_sample(const int sample_rate, const std::string & file_name, const std::vector<std::vector<double> > & sample_data, const unsigned sample_sample_rate) :
	sound(sample_rate, sample_rate / 2),
	file_name(file_name),
	samples(sample_data),
	sample_sample_rate(sample_sample_rate)
{
}

bool sound_sample::begin()
{
	if (samples.empty() == true) {
		auto            rc = load_sample(file_name);
		if (rc.has_value() == false) {
			printf("Cannot access sample \"%s\" in cache\n", file_name.c_str());
			return false;
		}
		samples            = *std::get<0>(rc.value());
		sample_sample_rate =  std::get<1>(rc.value());
		base_frequency     =  ceil(std::get<2>(rc.value()));
	}
	else {
		base_frequency     = find_loudest_frequency(samples, sample_sample_rate);
	}

	base_midi_note     = frequency_to_midi_note(base_frequency);
	name               = midi_note_to_name(base_midi_note);
	delta_t            = sample_sample_rate / double(sample_rate);

	input_output_matrix.resize(samples.at(0).size());

	printf("Sample %s has %zu channel(s), is sampled at %u Hz and sounds like a %s (%.2f Hz)\n", file_name.c_str(), input_output_matrix.size(), sample_sample_rate, name.c_str(), base_frequency);

	return true;
}

std::string sound_sample::get_name() const
{
	return name;
}

std::pair<double, std::map<int, double> > sound_sample::get_sample(const size_t channel_nr)
{
	double use_t = t;
	if (use_t < 0)
		use_t += ceil(fabs(use_t) / samples.size()) * samples.size();

	size_t offset = fmod(use_t, samples.size());

	return { samples.at(offset).at(channel_nr), input_output_matrix[channel_nr] };
}
