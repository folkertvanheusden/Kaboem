#include <cmath>
#include <mutex>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include "gui.h"
#include "pipewire-audio.h"


void on_process_audio(void *userdata);

static void on_state_changed(void *data, enum pw_stream_state old, enum pw_stream_state state, const char *error)
{
//	printf("%d --> %d | %s\n", old, state, error);
}

void configure_pipewire_audio(sound_parameters *const target)
{
	const char prog_name[] = PROG_NAME;

	target->pw.th = new std::thread([prog_name, target]() {
			target->pw.b    = SPA_POD_BUILDER_INIT(target->pw.buffer, sizeof(target->pw.buffer));

			target->pw.loop = pw_main_loop_new(nullptr);
			if (!target->pw.loop)
				fprintf(stderr, "pw_main_loop_new failed\n");

			target->pw.stream_events.version       = PW_VERSION_STREAM_EVENTS;
			target->pw.stream_events.process       = on_process_audio;
			target->pw.stream_events.state_changed = on_state_changed;

			target->pw.stream = pw_stream_new_simple(
					pw_main_loop_get_loop(target->pw.loop),
					prog_name,
					pw_properties_new(
						PW_KEY_APP_NAME, prog_name,
						PW_KEY_NODE_NAME, prog_name,
						PW_KEY_MEDIA_TYPE, "Audio",
						PW_KEY_MEDIA_CATEGORY, "Playback",
						PW_KEY_MEDIA_ROLE, "Music",
						nullptr),
					&target->pw.stream_events,
					target);
			if (!target->pw.stream)
				fprintf(stderr, "pw_stream_new_simple failed\n");

			memset(target->pw.saiw.position, 0x00, sizeof target->pw.saiw.position);

			target->pw.saiw.flags    = 0;
			target->pw.saiw.format   = SPA_AUDIO_FORMAT_F64;
			target->pw.saiw.channels = target->n_channels;
			target->pw.saiw.rate     = target->sample_rate;

			target->pw.params[0] = spa_format_audio_raw_build(&target->pw.b, SPA_PARAM_EnumFormat, &target->pw.saiw);

			if (pw_stream_connect(target->pw.stream,
					PW_DIRECTION_OUTPUT,
					PW_ID_ANY,
					pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
					target->pw.params, 1))
				fprintf(stderr, "pw_stream_connect failed\n");

			if (pw_main_loop_run(target->pw.loop))
				fprintf(stderr, "pw_main_loop_run failed\n");

			pw_stream_destroy   (target->pw.stream);
			pw_main_loop_destroy(target->pw.loop  );

			printf("pipewire thread terminating\n");
	});
}
