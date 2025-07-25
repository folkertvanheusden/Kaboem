#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#include "frequencies.h"
#include "gui.h"
#include "sf2.h"


std::string get_string(const uint8_t *const p, const size_t len)
{
	std::string out;
	out.assign(reinterpret_cast<const char *>(p), len);
	return out;
}

uint16_t get_WORD(const uint8_t *const p)
{
	return p[0] | (p[1] << 8);
}

uint32_t get_DWORD(const uint8_t *const p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

typedef struct
{
	std::string id;
	uint32_t size;
	const uint8_t *data;
} gen_block_t;

gen_block_t * read_block(const uint8_t *const p, const size_t size)
{
	if (size < 8)
		return NULL;

	gen_block_t *gb = new gen_block_t;

	gb->id = get_string(p, 4);

	gb->size = get_DWORD(&p[4]);

	gb->data = p + 8;

	return gb;
}

void process_riff_block(std::map<std::string, gen_block_t *> *const sf2_map, const uint8_t *const p, const size_t size)
{
	size_t i = 0;

	while(i < size) {
		gen_block_t *h = read_block(&p[i], size - i);
		if (!h)
			break;

		printf("block %s %u\n", h->id.c_str(), h->size);

		if (h->id == "RIFF") {
			std::string form = get_string(h->data, 4);
			printf("\t%s\n", form.c_str());

			process_riff_block(sf2_map, h->data + 4, h->size - 4);
		}
		else if (h->id == "LIST") {
			std::string form = get_string(h->data, 4);
			printf("\t%s\n", form.c_str());

			process_riff_block(sf2_map, h->data + 4, h->size - 4);
		}

		size_t inc = h->size + 8;
		if (h->size & 1)
			inc++;

		i += inc;

		sf2_map->insert(std::pair<std::string, gen_block_t *>(h->id, h));
	}
}

std::vector<float> load_sf2_sample(const std::map<std::string, gen_block_t *> *const sf2_map, const uint32_t dwStart, const uint32_t dwEnd)
{
	std::map<std::string, gen_block_t *>::const_iterator it = sf2_map->find("smpl");
	if (it == sf2_map->end())
		return { };

	gen_block_t *smpl = it->second;
	if (!smpl) {
		printf("\"smpl\" not found in sf2 file\n");
		return { };
	}

	size_t n = dwEnd - dwStart;
	std::vector<float> out(n);
	const int16_t *const p = reinterpret_cast<const int16_t *>(smpl->data);
	for(size_t i=0; i<n; i++)
		out[i] = p[dwStart + i] / 32768.f;

	return out;
}

void show_sf2_meta(const std::map<std::string, gen_block_t *> & sf2_map, const char *const tag)
{
	std::map<std::string, gen_block_t *>::const_iterator it = sf2_map.find(tag);
	if (it == sf2_map.end())
		return;

	gen_block_t *tags = it->second;

	printf("%s: %s\n", tag, get_string(tags->data, tags->size).c_str());
}

void dump_sf2(const std::map<std::string, gen_block_t *> & sf2_map, const char *const tag)
{
	std::map<std::string, gen_block_t *>::const_iterator it = sf2_map.find(tag);
	if (it == sf2_map.end())
		return;

	gen_block_t *tags = it->second;

	printf("%s: ", tag);
	for(size_t i=0; i<tags->size; i++)
		printf("%02x ", tags->data[i]);
	printf("\n");
}

sf2_sample_t load_sf2_sample(const std::map<std::string, gen_block_t *> *const sf2_map, const gen_block_t *const shdr, const size_t nr, const std::string & name)
{
	size_t idx = nr * 46;

	std::string local_name = get_string(&shdr->data[idx + 0], 20);
	if (local_name == "EOS")
		return { };

	uint32_t dwStart = get_DWORD(&shdr->data[idx + 20]);
	uint32_t dwEnd = get_DWORD(&shdr->data[idx + 24]);
	uint32_t dwLoopStart = get_DWORD(&shdr->data[idx + 28]);
	uint32_t dwLoopEnd = get_DWORD(&shdr->data[idx + 32]);
	uint32_t dwSampleRate = get_DWORD(&shdr->data[idx + 36]);
	uint8_t key = shdr->data[idx + 40];
	int8_t pitchCorrection = shdr->data[idx + 41];
	uint16_t sampleLink = get_WORD(&shdr->data[idx + 42]);
	uint16_t sampleType = get_WORD(&shdr->data[idx + 44]);

	if (nr > 0 && sampleLink == 0)
		sampleLink = nr;

	size_t n = dwEnd - dwStart;
	if (n == 0)
		return { };

	sf2_sample_t s { };
	s.filename = name.empty() ? local_name : name;
	s.sample_rate = dwSampleRate;

	if (sampleType == 1) {
		s.samples.resize(1);
		s.repeat_start[0] = dwLoopStart - dwStart;
		s.repeat_end[0] = dwLoopEnd - dwStart;
		s.samples[0] = load_sf2_sample(sf2_map, dwStart, dwEnd);

		s.repeat_start[1] = s.repeat_end[1] = -1;
	}
	else if (sampleType == 2) {
		s.samples.resize(2);
		s.repeat_start[1] = dwLoopStart - dwStart;
		s.repeat_end[1] = dwLoopEnd - dwStart;
		s.samples[1] = load_sf2_sample(sf2_map, dwStart, dwEnd);

		uint32_t dwStart2 = get_DWORD(&shdr->data[sampleLink * 46 + 20]);
		uint32_t dwEnd2 = get_DWORD(&shdr->data[sampleLink * 46 + 24]);

		uint32_t dwLoopStart2 = get_DWORD(&shdr->data[sampleLink * 46 + 28]);
		uint32_t dwLoopEnd2 = get_DWORD(&shdr->data[sampleLink * 46 + 32]);

		printf("\tstereo loops %d-%d %d-%d\n", dwLoopStart - dwStart, dwLoopEnd - dwStart, dwLoopStart2 - dwStart2, dwLoopEnd2 - dwStart2);

		s.repeat_start[0] = dwLoopStart2 - dwStart2;
		s.repeat_end[0] = dwLoopEnd2 - dwStart2;

		s.samples[0] = load_sf2_sample(sf2_map, dwStart2, dwEnd2);
	}
	else if (sampleType == 4) {
		s.samples.resize(2);
		s.repeat_start[0] = dwLoopStart - dwStart;
		s.repeat_end[0] = dwLoopEnd - dwStart;
		s.samples[0] = load_sf2_sample(sf2_map, dwStart, dwEnd);

		uint32_t dwStart2 = get_DWORD(&shdr->data[sampleLink * 46 + 20]);
		uint32_t dwEnd2 = get_DWORD(&shdr->data[sampleLink * 46 + 24]);

		uint32_t dwLoopStart2 = get_DWORD(&shdr->data[sampleLink * 46 + 28]);
		uint32_t dwLoopEnd2 = get_DWORD(&shdr->data[sampleLink * 46 + 32]);

		printf("\tstereo loops %d-%d %d-%d\n", dwLoopStart - dwStart, dwLoopEnd - dwStart, dwLoopStart2 - dwStart2, dwLoopEnd2 - dwStart2);

		s.repeat_start[1] = dwLoopStart2 - dwStart2;
		s.repeat_end  [1] = dwLoopEnd2 - dwStart2;

		s.samples[1]      = load_sf2_sample(sf2_map, dwStart2, dwEnd2);
	}
	else {
		printf("* Sample type unrecognized ** %d\n", sampleType);
		return { };
	}

	printf("\t%-20s\tsize: %u\tloop start: %u, loop end: %u, samplerate: %u, key: %u, pitch: %d, link: %u, type: %04x\n", name.c_str(), dwEnd - dwStart, dwLoopStart - dwStart, dwLoopEnd - dwStart, dwSampleRate, key, pitchCorrection, sampleLink, sampleType);

	return s;
}

sample_set_t alloc_sample_set()
{
	sample_set_t ss { };
	for(size_t i=0; i<128; i++)
		ss.sample_map[i] = -1;
	return ss;
}

auto add_instrument_bank_to_sample_set(std::map<uint16_t, sample_set_t> *const sets, const uint16_t instrument, const std::string & name, const bool isPercussion, sf2_sample_t *const s)
{
	std::map<uint16_t, sample_set_t>::iterator out;
	auto ss_it = sets->find(instrument);

	sample_set_t ss { };
	if (ss_it == sets->end()) {
		ss = alloc_sample_set();
		ss.name = name;
		out = sets->insert(std::pair<uint16_t, sample_set_t>(instrument, ss)).first;
	}
	else {
		ss = ss_it->second;
		out = ss_it;
	}

	ss.samples.push_back(*s);

	return out;
}

class INST
{
private:
	gen_block_t *inst { nullptr };

public:
	INST(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_inst = sf2_map.find("inst");
		if (it_inst != sf2_map.end())
			inst = it_inst->second;
	}

	size_t size()
	{
		if (!inst)
			return 0;
		return inst->size / 22;
	}

	std::string getName(const int ndx)
	{
		assert(ndx < size());
                return get_string(&inst->data[ndx * 22 + 0], 20);
	}

	int getBagIndex(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&inst->data[ndx * 22 + 20]);
	}
};

class IBAG
{
private:
	gen_block_t *ibag { nullptr };

public:
	IBAG(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_ibag = sf2_map.find("ibag");
		if (it_ibag != sf2_map.end())
			ibag = it_ibag->second;
	}

	size_t size()
	{
		if (!ibag)
			return 0;
		return ibag->size / 4;
	}

	// The WORD wInstGenNdx is an index to the instrument zone’s list of generators in the IGEN sub-chunk
	int getwInstGenNdx(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&ibag->data[ndx * 4 + 0]);
	}

	// and the wInstModNdx is an index to its list of modulators in the IMOD sub-chunk
	int getwInstModNdx(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&ibag->data[ndx * 4 + 2]);
	}
};

class IGEN
{
private:
	gen_block_t *igen { nullptr };

public:
	IGEN(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_igen = sf2_map.find("igen");
		if (it_igen != sf2_map.end())
			igen = it_igen->second;
	}

	size_t size()
	{
		if (!igen)
			return 0;
		return igen->size / 4;
	}

	int getsfGenOper(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&igen->data[ndx * 4 + 0]);
	}

	int getgenAmount(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&igen->data[ndx * 4 + 2]);
	}
};

class PHDR
{
private:
	gen_block_t *phdr { nullptr };

public:
	PHDR(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_phdr = sf2_map.find("phdr");
		if (it_phdr != sf2_map.end())
			phdr = it_phdr->second;
	}

	size_t size()
	{
		if (!phdr)
			return 0;
		return phdr->size / 38;
	}

	std::string getName(const int ndx)
	{
		assert(ndx < size());
                return get_string(&phdr->data[ndx * 38 + 0], 20);
	}

	int getwPreset(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&phdr->data[ndx * 38 + 20]);
	}

	int getwBank(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&phdr->data[ndx * 38 + 22]);
	}

	int getwPresetBagNdx(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&phdr->data[ndx * 38 + 24]);
	}

	int getwPresetBagNdxEnd(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&phdr->data[(ndx + 1) * 38 + 24]);
	}
};

class PBAG
{
private:
	gen_block_t *pbag { nullptr };

public:
	PBAG(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_pbag = sf2_map.find("pbag");
		if (it_pbag != sf2_map.end())
			pbag = it_pbag->second;
	}

	size_t size()
	{
		if (!pbag)
			return 0;
		return pbag->size / 4;
	}

	int getwGenNdx(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&pbag->data[ndx * 4 + 0]);
	}

	int getwModNdx(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&pbag->data[ndx * 4 + 2]);
	}
};

class PGEN
{
private:
	gen_block_t *pgen { nullptr };

public:
	PGEN(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_pgen = sf2_map.find("pgen");
		if (it_pgen != sf2_map.end())
			pgen = it_pgen->second;
	}

	size_t size()
	{
		if (!pgen)
			return 0;
		return pgen->size / 4;
	}

	int getsfGenOper(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&pgen->data[ndx * 4 + 0]);
	}

	int getgenAmount(const int ndx)
	{
		assert(ndx < size());
                return get_WORD(&pgen->data[ndx * 4 + 2]);
	}
};

class SHDR
{
private:
	gen_block_t *shdr { nullptr };

public:
	SHDR(const std::map<std::string, gen_block_t *> & sf2_map)
	{
		std::map<std::string, gen_block_t *>::const_iterator it_shdr = sf2_map.find("shdr");
		if (it_shdr != sf2_map.end())
			shdr = it_shdr->second;
	}

	size_t size()
	{
		if (!shdr)
			return 0;
		return shdr->size / 46;
	}

	std::string getName(const int ndx)
	{
		assert(ndx < size());
                return get_string(&shdr->data[ndx * 46 + 0], 20);
	}

	const gen_block_t *getPointer() const
	{
		return shdr;
	}
};

auto add_instrument_to_sample_set(std::map<uint16_t, sample_set_t> *const sets, const uint16_t bank_instrument, const std::string & name, const bool isPercussion, sf2_sample_t & s)
{
	std::map<uint16_t, sample_set_t>::iterator out;

	printf("bank_instrument: %u, name %s, filename %s\n", bank_instrument, name.c_str(), s.filename.c_str());

        auto ss_it = sets->find(bank_instrument);

        sample_set_t ss { };
        if (ss_it == sets->end()) {
                ss = alloc_sample_set();
                ss.name = name;
		ss.samples.push_back(s);
                out = sets->insert(std::pair<uint16_t, sample_set_t>(bank_instrument, ss)).first;
        }
        else {
                ss = ss_it->second;
		ss.samples.push_back(s);
		out = ss_it;
        }

        return out;
}

std::map<uint16_t, sample_set_t> load_sf2(const std::string & filename, const bool isPercussion)
{
	printf("Loading sf2 %s...\n", filename.c_str());

	FILE *fh = fopen(filename.c_str(), "rb");
	if (!fh)
		return { };

	std::map<uint16_t, sample_set_t> sets;

	struct stat st { };
	fstat(fileno(fh), &st);

	size_t   size = st.st_size;
	uint8_t *data = new uint8_t[size]();
	bool     ok   = fread(data, 1, size, fh) == size;
	fclose(fh);
	if (!ok)
		return { };

	std::map<std::string, gen_block_t *> sf2_map;
	process_riff_block(&sf2_map, data, size);

	show_sf2_meta(sf2_map, "isng");
	show_sf2_meta(sf2_map, "irom");
	show_sf2_meta(sf2_map, "INAM");
	show_sf2_meta(sf2_map, "ICRD");
	show_sf2_meta(sf2_map, "IENG");
	show_sf2_meta(sf2_map, "IPRD");
	show_sf2_meta(sf2_map, "ICOP");
	show_sf2_meta(sf2_map, "ICMT");
	show_sf2_meta(sf2_map, "ISFT");

	PHDR phdr(sf2_map);
	PBAG pbag(sf2_map);
	PGEN pgen(sf2_map);
	INST inst(sf2_map);
	IBAG ibag(sf2_map);
	IGEN igen(sf2_map);
	SHDR shdr(sf2_map);

	for(size_t phdr_ndx=0; phdr_ndx<phdr.size() - 1; phdr_ndx++) {
		int wBank = phdr.getwBank(phdr_ndx);
		int wPreset = phdr.getwPreset(phdr_ndx);
		printf("Loading preset %s (%d,%d)\n", phdr.getName(phdr_ndx).c_str(), wBank, wPreset);

		int presetNdxStart = phdr.getwPresetBagNdx(phdr_ndx);
		int presetNdxEnd = phdr.getwPresetBagNdxEnd(phdr_ndx);

		for(int presetNdx = presetNdxStart; presetNdx < presetNdxEnd; presetNdx++) {
			size_t wGenNdx = pbag.getwGenNdx(presetNdx);

			while(wGenNdx < pgen.size()) {
				int sfGenOper = pgen.getsfGenOper(wGenNdx);
				int genAmount = pgen.getgenAmount(wGenNdx);

				if (sfGenOper == 41) {
					// genAmount is instrument ndx in INST
					// from there we need to follow the path to IGEN where the IGEN
					// index points to the SHDR index which is the midi-instrument number

					std::string name = inst.getName(genAmount);
					printf("\tinstrument %s\n", name.c_str());

					int wInstBagNdx = inst.getBagIndex(genAmount);

					size_t wInstGenNdx = ibag.getwInstGenNdx(wInstBagNdx);
					// size_t wInstModNdx = ibag.getwInstModNdx(wInstBagNdx);

					int midi_note_start = -1, midi_note_end = -1;
					int loopStart = -1, loopEnd = -1;
					bool loopSet = false;
					int key = -1;
					int sample_id = -1;

					while(wInstGenNdx < igen.size()) {
						int sfGenOper = igen.getsfGenOper(wInstGenNdx);
						int genAmount = igen.getgenAmount(wInstGenNdx);

						if (sfGenOper == 2) { // startLoopAddrsOffset
							loopSet = true;
							loopStart = int16_t(genAmount);
						}
						else if (sfGenOper == 3) { //endLoopAddrsOffset
							loopSet = true;
							loopEnd = int16_t(genAmount);
						}
						else if (sfGenOper == 43)  { // keyrange
							midi_note_start = genAmount & 255;
							midi_note_end = genAmount >> 8;
						}
						else if (sfGenOper == 45) { // startloopAddrsCoarseOffset
							loopStart += genAmount;
						}
						else if (sfGenOper == 46) { // keynum
							if (genAmount > 127)
								printf("\t*** ignoring keynum\n");
							else
								key = genAmount;
						}
						else if (sfGenOper == 50) { // endloopAddrsCoarseOffset
							loopEnd += genAmount;
						}
						else if (sfGenOper == 53) {
							sample_id = genAmount;
						}
						else if (sfGenOper == 58) {  // overridingRootKey
							key = genAmount;
						}

						wInstGenNdx++;
					}

					if (sample_id != -1) {
						std::string sample_name = shdr.getName(sample_id);

						printf("\tSample name: %s\n", sample_name.c_str());
						printf("\tSet key range %d - %d\n", midi_note_start, midi_note_end);

						sf2_sample_t s = load_sf2_sample(&sf2_map, shdr.getPointer(), genAmount, name);

						if (loopSet) {
							s.repeat_start[0] += loopStart;
							s.repeat_end[0] += loopEnd;
							printf("\tloop left/mono %zu->%zu\n", s.repeat_start[0], s.repeat_end[0]);

							if (s.samples.size() >= 2) {
								s.repeat_start[1] += loopStart;
								s.repeat_end[1] += loopEnd;
								printf("\tloop right %zu->%zu\n", s.repeat_start[1], s.repeat_end[1]);
							}
						}

						printf("\tSet keynum %d\n", key);

						s.key = key;
						if (key != -1) {
							s.base_freq = midi_note_to_frequency(key);
							printf("\tbase freq overriden to %.1fhz\n", s.base_freq);
						}

						// FIXME need to have multiple samples per bank/preset; per key(-range)
						// add to sample_set
						auto it = add_instrument_to_sample_set(&sets, (wBank << 8) | wPreset, name, wBank == 128, s);

						if (midi_note_start != -1 && midi_note_end != -1) {
							for(int n = midi_note_start; n<=midi_note_end; n++)
								it->second.sample_map[n] = it->second.samples.size() - 1;
						}
					}

					break;
				}

				wGenNdx++;
			}
		}
	}

	std::map<std::string, gen_block_t *>::iterator cit = sf2_map.begin();
	for(;cit != sf2_map.end(); cit++)
		delete cit->second;

	delete [] data;

	return sets;
}

sample convert_sf2_sample(sf2_sample_t *const in)
{
	sample out { };
	out.echo_t = 0;
	if (in->key != -1)
		out.midi_note = in->key;
	out.name   = in->filename;

	std::vector<std::vector<float> > samples;
	if (in->samples.size() >= 2) {
		size_t use_n_samples = std::min(in->samples.at(0).size(), in->samples.at(1).size());
		printf("\"%s\" is stereo, %zu samples\n", out.name.c_str(), use_n_samples);
		samples.reserve(use_n_samples);
		for(size_t i=0; i<use_n_samples; i++) {
			std::vector<float> pair { in->samples.at(0).at(i), in->samples.at(1).at(i) };
			samples.push_back(pair);
		}
	}
	else {
		size_t use_n_samples = in->samples.at(0).size();
		printf("\"%s\" is mono, %zu samples\n", out.name.c_str(), use_n_samples);
		samples.reserve(use_n_samples);
		for(size_t i=0; i<use_n_samples; i++) {
			std::vector<float> pair { in->samples.at(0).at(i) };
			samples.push_back(pair);
		}
	}

	out.s   = new sound_sample(sample_rate, out.name, samples, in->sample_rate);
	auto rc = out.s->begin();
	if (rc.has_value())
		printf("error: %s\n", rc.value().c_str());
	else {
		out.s->set_volume(0, 1.);
		if (in->samples.size() >= 2)
			out.s->set_volume(1, 1.);

		if (in->repeat_start[0] != size_t(-1)) {
			printf("Can repeat: %zu %zu\n", in->repeat_start[0], in->repeat_end[0]);
			out.s->set_repeat(in->repeat_start[0], in->repeat_end[0]);
		}
	}
	return out;
}
