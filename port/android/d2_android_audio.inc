/* Android host audio backend for ps3recomp cellAudio (included by cellAudio.c on __ANDROID__).
 * SDL's audio driver needs its Java side, which this build does not have, so the mixer writes straight to
 * AAudio: float stereo, 48 kHz, shared low-latency stream, non-blocking writes. Same contract as the WASAPI
 * backend: init / shutdown / submit / room / prime / wait. */
#include <aaudio/AAudio.h>
#include <time.h>

static AAudioStream* s_aa_stream = NULL;
static int32_t       s_aa_cap = 0;

static int audio_backend_init(void)
{
    AAudioStreamBuilder* b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) {
        printf("[cellAudio] AAudio: cannot create a stream builder\n");
        return -1;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s_aa_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s_aa_stream) {
        printf("[cellAudio] AAudio: openStream failed: %s\n", AAudio_convertResultToText(r));
        s_aa_stream = NULL;
        return -1;
    }
    /* Headroom for a hitch in the guest: about five mixer blocks, never less than three bursts. */
    const int32_t burst = AAudioStream_getFramesPerBurst(s_aa_stream);
    int32_t want = CELL_AUDIO_BLOCK_SAMPLES * 5;
    if (want < burst * 3) want = burst * 3;
    AAudioStream_setBufferSizeInFrames(s_aa_stream, want);
    s_aa_cap = AAudioStream_getBufferSizeInFrames(s_aa_stream);
    const aaudio_result_t st = AAudioStream_requestStart(s_aa_stream);
    printf("[cellAudio] AAudio: %d Hz, %d ch, burst %d, buffer %d frames, start %s\n",
           AAudioStream_getSampleRate(s_aa_stream), AAudioStream_getChannelCount(s_aa_stream),
           (int)burst, (int)s_aa_cap, AAudio_convertResultToText(st));
    return 0;
}

static void audio_backend_shutdown(void)
{
    if (!s_aa_stream) return;
    AAudioStream_requestStop(s_aa_stream);
    AAudioStream_close(s_aa_stream);
    s_aa_stream = NULL;
}

/* Periodic device report (same switch as the mixer's own AUDIO_RATE trace). */
static void audio_aa_report(u32 frames, u32 dropped, const float* s)
{
    static int on = -1;
    static unsigned long long t0, nblk, nsub, ndrop, nsilent;
    static float peak;
    if (on < 0) on = getenv("AUDIO_RATE") != NULL;
    if (!on) return;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    const unsigned long long now = (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
    if (!t0) t0 = now;
    float pk = 0.0f;
    for (u32 i = 0; i < frames * 2; i++) { const float a = s[i] < 0 ? -s[i] : s[i]; if (a > pk) pk = a; }
    if (pk > peak) peak = pk;
    nblk++; nsub += frames; ndrop += dropped; if (pk == 0.0f) nsilent++;
    if (now - t0 >= 5000000000ULL) {
        const int64_t queued = AAudioStream_getFramesWritten(s_aa_stream) - AAudioStream_getFramesRead(s_aa_stream);
        printf("[cellAudio] AAudio 5s: %llu blocks (%.1f/s), submitted %llu frames, device refused %llu, silent blocks %llu, "
               "peak %.3f, xruns %d, queued %lld/%d\n",
               nblk, nblk * 1e9 / (double)(now - t0), nsub, ndrop, nsilent, peak,
               (int)AAudioStream_getXRunCount(s_aa_stream), (long long)queued, (int)s_aa_cap);
        fflush(stdout);
        t0 = now; nblk = nsub = ndrop = nsilent = 0; peak = 0.0f;
    }
}

static void audio_backend_submit(const float* stereo_samples, u32 num_samples)
{
    if (!s_aa_stream || !num_samples) return;
    const aaudio_result_t w = AAudioStream_write(s_aa_stream, stereo_samples, (int32_t)num_samples, 0);
    const u32 done = w > 0 ? (u32)w : 0;
    if (done < num_samples) g_audio_dropped += num_samples - done;
    if (num_samples == CELL_AUDIO_BLOCK_SAMPLES) audio_aa_report(num_samples, num_samples - done, stereo_samples);
}

/* Frames the device can take right now; -1 with no device open. */
static int audio_backend_room(void)
{
    if (!s_aa_stream) return -1;
    const int64_t queued = AAudioStream_getFramesWritten(s_aa_stream) - AAudioStream_getFramesRead(s_aa_stream);
    const int64_t room = (int64_t)s_aa_cap - (queued > 0 ? queued : 0);
    return room > 0 ? (int)room : 0;
}

/* Refill an empty device with silence, never by advancing the guest ring. */
static u32 audio_backend_prime(void)
{
    static const float silence[CELL_AUDIO_BLOCK_SAMPLES * 4 * 2];
    if (!s_aa_stream) return 0;
    if (AAudioStream_getFramesWritten(s_aa_stream) - AAudioStream_getFramesRead(s_aa_stream) > 0) return 0;
    const u32 frames = CELL_AUDIO_BLOCK_SAMPLES * 2;
    audio_backend_submit(silence, frames);
    return frames;
}

static void audio_backend_wait(void)
{
    usleep(1000);
}
