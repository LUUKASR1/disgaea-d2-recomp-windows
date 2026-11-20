/* Android host audio backend for ps3recomp cellAudio (included by cellAudio.c on __ANDROID__).
 * SDL's audio driver needs its Java side, which this build does not have, so the mixer feeds AAudio directly:
 * float stereo, 48 kHz, shared low-latency stream in callback mode. The mixer thread fills a lock-free ring and
 * the AAudio callback drains it (zeros on underrun), so the mixer never depends on how the device counts frames.
 * Same contract as the WASAPI backend: init / shutdown / submit / room / prime / wait. */
#include <aaudio/AAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define AA_RING_FRAMES 16384u            /* power of two */
#define AA_RING_MASK   (AA_RING_FRAMES - 1u)

static AAudioStream*   s_aa_stream = NULL;
static float           s_aa_ring[AA_RING_FRAMES * 2];
static _Atomic uint64_t s_aa_wr, s_aa_rd;          /* frames written by the mixer / consumed by the device */
static uint32_t        s_aa_target = 0;            /* frames the mixer may keep queued */
static _Atomic uint64_t s_aa_callbacks, s_aa_underrun, s_aa_blocks, s_aa_refused, s_aa_silent;
static volatile float  s_aa_peak = 0.0f;
static pthread_t       s_aa_watch;
static volatile int    s_aa_watch_run = 0;

static aaudio_data_callback_result_t aa_data_callback(AAudioStream* stream, void* user, void* audio, int32_t frames)
{
    (void)stream; (void)user;
    float* out = (float*)audio;
    const uint64_t rd = atomic_load_explicit(&s_aa_rd, memory_order_relaxed);
    const uint64_t wr = atomic_load_explicit(&s_aa_wr, memory_order_acquire);
    const uint64_t avail = wr - rd;
    const uint32_t want = frames > 0 ? (uint32_t)frames : 0;
    const uint32_t take = avail < want ? (uint32_t)avail : want;
    for (uint32_t i = 0; i < take; i++) {
        const uint32_t idx = (uint32_t)((rd + i) & AA_RING_MASK);
        out[i * 2]     = s_aa_ring[idx * 2];
        out[i * 2 + 1] = s_aa_ring[idx * 2 + 1];
    }
    for (uint32_t i = take * 2; i < want * 2; i++) out[i] = 0.0f;
    if (take < want) atomic_fetch_add_explicit(&s_aa_underrun, want - take, memory_order_relaxed);
    atomic_store_explicit(&s_aa_rd, rd + take, memory_order_release);
    atomic_fetch_add_explicit(&s_aa_callbacks, 1, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Health report every 5 s from its own thread, so it still prints if the mixer thread stalls.
 * Enabled by the same AUDIO_RATE switch as the mixer's own trace. */
static void* aa_watch_thread(void* arg)
{
    (void)arg;
    unsigned long long p_blocks = 0, p_refused = 0, p_silent = 0, p_cb = 0, p_under = 0;
    while (s_aa_watch_run) {
        for (int i = 0; i < 25 && s_aa_watch_run; i++) { struct timespec ts = {0, 200000000L}; nanosleep(&ts, NULL); }
        if (!s_aa_watch_run || !s_aa_stream) break;
        const unsigned long long blocks = atomic_load(&s_aa_blocks), refused = atomic_load(&s_aa_refused),
                                 silent = atomic_load(&s_aa_silent), cb = atomic_load(&s_aa_callbacks),
                                 under = atomic_load(&s_aa_underrun);
        const uint64_t queued = atomic_load(&s_aa_wr) - atomic_load(&s_aa_rd);
        printf("[cellAudio] AAudio 5s: mixer %.1f blocks/s (real time 187.5), refused %llu frames, silent blocks %llu, "
               "peak %.3f | device %llu callbacks, underrun %llu frames, xruns %d, state %d, queued %llu/%u\n",
               (blocks - p_blocks) / 5.0, refused - p_refused, silent - p_silent, (double)s_aa_peak,
               cb - p_cb, under - p_under, (int)AAudioStream_getXRunCount(s_aa_stream),
               (int)AAudioStream_getState(s_aa_stream), (unsigned long long)queued, s_aa_target);
        fflush(stdout);
        p_blocks = blocks; p_refused = refused; p_silent = silent; p_cb = cb; p_under = under;
        s_aa_peak = 0.0f;
    }
    return NULL;
}

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
    AAudioStreamBuilder_setDataCallback(b, aa_data_callback, NULL);
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s_aa_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s_aa_stream) {
        printf("[cellAudio] AAudio: openStream failed: %s\n", AAudio_convertResultToText(r));
        s_aa_stream = NULL;
        return -1;
    }
    const int32_t burst = AAudioStream_getFramesPerBurst(s_aa_stream);
    AAudioStream_setBufferSizeInFrames(s_aa_stream, burst * 2);
    /* The device pulls a burst at a time: keep two bursts plus two mixer blocks queued, never less than five blocks. */
    uint32_t target = (uint32_t)burst * 2u + CELL_AUDIO_BLOCK_SAMPLES * 2u;
    if (target < CELL_AUDIO_BLOCK_SAMPLES * 5u) target = CELL_AUDIO_BLOCK_SAMPLES * 5u;
    if (target > AA_RING_FRAMES - CELL_AUDIO_BLOCK_SAMPLES) target = AA_RING_FRAMES - CELL_AUDIO_BLOCK_SAMPLES;
    s_aa_target = target;
    atomic_store(&s_aa_wr, 0); atomic_store(&s_aa_rd, 0);
    const aaudio_result_t st = AAudioStream_requestStart(s_aa_stream);
    printf("[cellAudio] AAudio (callback): %d Hz, %d ch, burst %d, device buffer %d frames, ring target %u frames, start %s\n",
           AAudioStream_getSampleRate(s_aa_stream), AAudioStream_getChannelCount(s_aa_stream), (int)burst,
           (int)AAudioStream_getBufferSizeInFrames(s_aa_stream), s_aa_target, AAudio_convertResultToText(st));
    if (getenv("AUDIO_RATE")) {
        s_aa_watch_run = 1;
        if (pthread_create(&s_aa_watch, NULL, aa_watch_thread, NULL) != 0) s_aa_watch_run = 0;
    }
    return 0;
}

/* The app went to the background (paused=1) or came back (paused=0). Called from the render thread, which is also the
 * guest's frame pacing, so the game is already stopped; this keeps the device from looping the last audio meanwhile. */
void d2_android_audio_pause(int paused)
{
    if (!s_aa_stream) return;
    if (paused) {
        AAudioStream_requestPause(s_aa_stream);
        printf("[cellAudio] AAudio paused (app in the background)\n");
    } else {
        /* Drop what was queued before the pause so it is not heard again. */
        atomic_store(&s_aa_rd, atomic_load(&s_aa_wr));
        const aaudio_result_t st = AAudioStream_requestStart(s_aa_stream);
        printf("[cellAudio] AAudio resumed: %s\n", AAudio_convertResultToText(st));
    }
    fflush(stdout);
}

static void audio_backend_shutdown(void)
{
    if (s_aa_watch_run) { s_aa_watch_run = 0; pthread_join(s_aa_watch, NULL); }
    if (!s_aa_stream) return;
    AAudioStream_requestStop(s_aa_stream);
    AAudioStream_close(s_aa_stream);
    s_aa_stream = NULL;
}

static void audio_backend_submit(const float* stereo_samples, u32 num_samples)
{
    if (!s_aa_stream || !num_samples) return;
    const uint64_t wr = atomic_load_explicit(&s_aa_wr, memory_order_relaxed);
    const uint64_t rd = atomic_load_explicit(&s_aa_rd, memory_order_acquire);
    const uint64_t used = wr - rd;
    const uint32_t free_frames = used >= AA_RING_FRAMES ? 0u : (uint32_t)(AA_RING_FRAMES - used);
    const uint32_t n = num_samples < free_frames ? num_samples : free_frames;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t idx = (uint32_t)((wr + i) & AA_RING_MASK);
        s_aa_ring[idx * 2]     = stereo_samples[i * 2];
        s_aa_ring[idx * 2 + 1] = stereo_samples[i * 2 + 1];
    }
    atomic_store_explicit(&s_aa_wr, wr + n, memory_order_release);
    if (n < num_samples) { g_audio_dropped += num_samples - n; atomic_fetch_add(&s_aa_refused, num_samples - n); }
    if (num_samples == CELL_AUDIO_BLOCK_SAMPLES) {
        float pk = 0.0f;
        for (u32 i = 0; i < num_samples * 2; i++) { const float a = stereo_samples[i] < 0 ? -stereo_samples[i] : stereo_samples[i]; if (a > pk) pk = a; }
        if (pk > s_aa_peak) s_aa_peak = pk;
        if (pk == 0.0f) atomic_fetch_add(&s_aa_silent, 1);
        atomic_fetch_add(&s_aa_blocks, 1);
    }
}

/* Frames the mixer may add right now; -1 with no device open. */
static int audio_backend_room(void)
{
    if (!s_aa_stream) return -1;
    const uint64_t queued = atomic_load_explicit(&s_aa_wr, memory_order_relaxed) - atomic_load_explicit(&s_aa_rd, memory_order_acquire);
    return queued >= s_aa_target ? 0 : (int)(s_aa_target - queued);
}

/* Refill an empty device with silence, never by advancing the guest ring. */
static u32 audio_backend_prime(void)
{
    static const float silence[CELL_AUDIO_BLOCK_SAMPLES * 4 * 2];
    if (!s_aa_stream) return 0;
    if (atomic_load_explicit(&s_aa_wr, memory_order_relaxed) - atomic_load_explicit(&s_aa_rd, memory_order_acquire) > 0) return 0;
    const u32 frames = CELL_AUDIO_BLOCK_SAMPLES * 2;   /* not a single block, so it is not counted as mixed audio */
    audio_backend_submit(silence, frames);
    return frames;
}

static void audio_backend_wait(void)
{
    usleep(1000);
}
