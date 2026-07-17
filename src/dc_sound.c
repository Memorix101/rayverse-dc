
// Dreamcast sound backend.
// Sound effects: the game's software mixer (game_get_sound_samples) produces interleaved
// stereo s16 at 44100 Hz. We feed it to the AICA through KOS's snd_stream API, which pulls
// data via a callback whenever snd_stream_poll() finds room in the SPU-side ring buffer.
//
// Music (dc_music_*): two backends, selected with DC_MUSIC_CDDA (default 0):
//   0 = the .ogg files from the data track, decoded by kos-ports' Tremor (integer Vorbis)
//       in its own thread. The game's stb_vorbis path is NOT used on the Dreamcast: a float
//       decoder inside the 60 Hz frame budget is too slow on the SH-4 and the result is
//       crackling/broken audio.
//   1 = Red Book CDDA played by the GD-ROM drive (needs audio tracks on the disc).

static snd_stream_hnd_t dc_sound_stream = SND_STREAM_INVALID;

// The AICA driver requests data in bytes (the smp_req parameter is a byte count, despite
// its name). We hand it samples straight out of the game's mixer.
static void* dc_sound_stream_callback(snd_stream_hnd_t hnd, int bytes_req, int* bytes_recv) {
    (void) hnd;
    game_sound_buffer_t* sound_buffer = &global_app_state.game.sound_buffer;
    s32 bytes_per_sample = 2 * sizeof(s16); // stereo, 16-bit

    // The buffer allocated by game_init_sound() holds one second of audio.
    s32 max_bytes = sound_buffer->samples_per_second * bytes_per_sample;
    if (bytes_req > max_bytes) {
        bytes_req = max_bytes;
    }

    sound_buffer->sample_count = bytes_req / bytes_per_sample;
    game_get_sound_samples(sound_buffer);

    *bytes_recv = sound_buffer->sample_count * bytes_per_sample;
    return sound_buffer->samples;
}

// SPU-side ring buffer size == audio latency, and half of it is the mixer granularity:
// the game's mixer only renders when the stream requests a half-buffer, so sounds that get
// triggered and interrupted within one window are lost entirely. 8KB at 44.1kHz stereo s16
// is ~45ms latency with ~23ms windows — close to the PC build's per-frame (16ms) mixing.
// (The 64KB SND_STREAM_BUFFER_MAX default adds ~370ms of delay, which is clearly audible.)
#define DC_SOUND_STREAM_BUFFER_SIZE (8 << 10)

void dc_init_sound(app_state_t* app_state) {
    snd_stream_init();
    dc_sound_stream = snd_stream_alloc(dc_sound_stream_callback, DC_SOUND_STREAM_BUFFER_SIZE);
    if (dc_sound_stream == SND_STREAM_INVALID) {
        printf("Error: couldn't allocate AICA sound stream\n");
        return;
    }
    app_state->dc.sound_output.samples_per_second = 44100;
    app_state->dc.sound_output.stream = dc_sound_stream;
    snd_stream_start(dc_sound_stream, 44100, 1 /*stereo*/);
}

void dc_poll_sound(void) {
    if (dc_sound_stream != SND_STREAM_INVALID) {
        snd_stream_poll(dc_sound_stream);
    }
}

#if !DC_MUSIC_CDDA

// --- Music via Tremor (integer Vorbis decoder, kos-ports) -------------------------------------
// sndoggvorbis runs its own decoder thread on top of snd_stream; it coexists with our sound
// effect stream (the AICA mixes both). The game addresses music by the track numbers of the
// original mixed-mode PC CD (2..26, track 1 was the data track, see doc/CDTRACKS.TXT); the
// files on the data track keep that numbering (Music/rayman02.ogg ...).

static bool dc_music_track_looping;
static s32 dc_music_current_track;
static u64 dc_music_start_ms;

void dc_music_init(void) {
    sndoggvorbis_init();
}

void dc_music_play(s32 game_track, bool looping) {
    if (game_track < 2 || game_track > 99) {
        return;
    }
    // The game's CD loop logic (TestCdLoop) restarts the current track based on a stubbed
    // DOS timer and fires far too often. Restarting means re-reading and re-parsing the
    // .ogg from disc (a visible hitch), and our looping tracks repeat by themselves anyway,
    // so a restart of the already-playing track is simply ignored.
    if (looping && dc_music_track_looping && game_track == dc_music_current_track &&
        sndoggvorbis_isplaying()) {
        return;
    }
    char path[64];
    snprintf(path, sizeof(path), DATA_DIR PATH_SEP "Music" PATH_SEP "rayman%02d.ogg", (int)game_track);
    if (sndoggvorbis_isplaying()) {
        sndoggvorbis_stop();
    }
    sndoggvorbis_start(path, looping ? 1 : 0);
    dc_music_track_looping = looping;
    dc_music_current_track = game_track;
    dc_music_start_ms = timer_ms_gettime64();
}

void dc_music_stop(void) {
    if (sndoggvorbis_isplaying()) {
        sndoggvorbis_stop();
    }
    dc_music_current_track = 0;
}

bool dc_music_playing(void) {
    return sndoggvorbis_isplaying() != 0;
}

// The game uses is_ogg_playing to detect the end of a non-looping music track (e.g. the
// Ubisoft logo waits for the intro jingle to finish). The PC build's OGG decoder clears the
// flag itself; here we mirror the state of the Tremor player thread. The startup grace
// period covers the moment right after sndoggvorbis_start() before the thread reports
// itself as playing.
void dc_music_update(void) {
    if (!is_ogg_playing || dc_music_track_looping || !MusicCdActive) {
        return;
    }
    if (timer_ms_gettime64() - dc_music_start_ms < 2000) {
        return;
    }
    if (!dc_music_playing()) {
        is_ogg_playing = false;
    }
}

#else

// --- Music via CD audio (Red Book) -------------------------------------------------------------
// The game requests the track numbers of the original mixed-mode PC CD, where track 1 is the
// data track and the audio tracks are numbered 2..26 (see doc/CDTRACKS.TXT). On our disc the
// audio tracks may sit elsewhere (e.g. an "audio/data" CDI has them starting at track 1), so
// we scan the TOC once for the first audio track and remap:
//   disc_track = game_track - 2 + first_audio_track
static u32 dc_cdda_first_track;
static u32 dc_cdda_last_track;
static bool dc_cdda_track_looping;
static u64 dc_cdda_start_ms;
static u64 dc_cdda_last_status_ms;

void dc_music_init(void) {
    CDROM_TOC toc;
    if (cdrom_read_toc(&toc, false) != ERR_OK) {
        printf("CDDA: couldn't read TOC, music disabled\n");
        return;
    }
    u32 first = TOC_TRACK(toc.first);
    u32 last = TOC_TRACK(toc.last);
    for (u32 track = first; track <= last; ++track) {
        u32 ctrl = TOC_CTRL(toc.entry[track - 1]);
        bool is_data = (ctrl & 4) != 0;
        if (!is_data) {
            if (!dc_cdda_first_track) {
                dc_cdda_first_track = track;
            }
            dc_cdda_last_track = track;
        }
    }
    if (dc_cdda_first_track) {
        printf("CDDA: audio tracks %lu..%lu found\n",
               (unsigned long)dc_cdda_first_track, (unsigned long)dc_cdda_last_track);
    } else {
        printf("CDDA: no audio tracks on disc, music disabled\n");
    }
}

bool dc_music_playing(void);

void dc_music_play(s32 game_track, bool looping) {
    if (!dc_cdda_first_track || game_track < 2) {
        return;
    }
    u32 disc_track = (u32)game_track - 2 + dc_cdda_first_track;
    if (disc_track > dc_cdda_last_track) {
        return;
    }
    // loops: 0 = play once, 15 = repeat forever
    cdrom_cdda_play(disc_track, disc_track, looping ? 15 : 0, CDDA_TRACKS);
    dc_cdda_track_looping = looping;
    dc_cdda_start_ms = timer_ms_gettime64();
}

// Same job as the Tremor variant above, but polling the drive status. The startup grace
// period covers the drive briefly reporting seeking/busy before playback begins.
void dc_music_update(void) {
    if (!dc_cdda_first_track || !is_ogg_playing || dc_cdda_track_looping || !MusicCdActive) {
        return;
    }
    u64 now = timer_ms_gettime64();
    if (now - dc_cdda_start_ms < 3000 || now - dc_cdda_last_status_ms < 500) {
        return;
    }
    dc_cdda_last_status_ms = now;
    if (!dc_music_playing()) {
        is_ogg_playing = false;
    }
}

void dc_music_stop(void) {
    if (dc_cdda_first_track) {
        cdrom_cdda_pause();
    }
}

bool dc_music_playing(void) {
    if (!dc_cdda_first_track) {
        return false;
    }
    int status = 0, disc_type = 0;
    if (cdrom_get_status(&status, &disc_type) != ERR_OK) {
        return false;
    }
    // Seeking/busy happens right after starting a track; count it as playing.
    return status == CD_STATUS_PLAYING || status == CD_STATUS_SEEKING || status == CD_STATUS_BUSY;
}

#endif // DC_MUSIC_CDDA
