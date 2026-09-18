/**
 * replay_voice.c — plays the replay announcer clip as a sequence of pieces.
 *
 * The DC sound driver caps one sample at 65534 frames, so the announcer clips
 * ship pre-split (split_replay_voice.py) into 2-3 contiguous pieces cut at
 * natural pauses. The pieces of the loaded clip sit in consecutive SFX slots
 * from REPLAY_VOICE_SLOT_FIRST, and this file starts each one at the moment
 * the previous one ends so the line plays as a single take.
 *
 * Timing is derived from the frame counts in the generated table rather than
 * from the backend, so the schedule is identical on SDL and DC even though
 * one stores decoded PCM and the other Yamaha ADPCM. Consecutive slots also
 * mean a piece can start while its predecessor is still sounding — there is
 * no slot to free first, so the boundary cannot glitch on a late frame.
 *
 * Shared by both targets: dc/ compiles this file from sdl/src/sound.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

#define REPLAY_VOICE_TABLE
#include "replay_voice.h"

/* The main loop runs at 30fps, so a piece can only be started on a 33 ms
 * boundary. Aiming half a frame early splits that error either side of the
 * ideal join instead of always landing late: a piece starts at most ~17 ms
 * early or ~17 ms late. Every cut sits in a pause of 68 ms or more, so the
 * join stays inside the pause in both directions. */
#define REPLAY_PIECE_LEAD_MS 16

static int          s_clipIndex = -1;   /* clip whose pieces are loaded, -1 = none */
static int          s_pieceCount;
static int          s_nextPiece;        /* first piece not yet started */
static int          s_playing;
static unsigned int s_pieceStartMs[REPLAY_MAX_PIECES];  /* absolute, from timeGetTime */
static unsigned int s_endMs;

static unsigned int PieceDurationMs(int clipIndex, int piece)
{
    return (unsigned int)(((unsigned long long)
        g_replayClipInfo[clipIndex].pieceSamples[piece] * 1000u) / REPLAY_SAMPLE_RATE);
}

/**
 * Loads every piece of one clip into its slot. Replaces the single
 * LoadSoundEffect(name, 0x38) the announcer used before it was split.
 */
void ReplayVoice_Load(int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= REPLAY_CLIP_COUNT) {
        return;
    }

    ReplayVoice_Stop();

    const ReplayClipInfo *info = &g_replayClipInfo[clipIndex];
    for (int i = 0; i < info->pieceCount; i++) {
        char filename[16];
        snprintf(filename, sizeof(filename), "%s.WAV", info->pieceName[i]);
        LoadSoundEffect(filename, REPLAY_VOICE_SLOT(i));
    }

    s_clipIndex = clipIndex;
    s_pieceCount = info->pieceCount;
}

/**
 * Starts the loaded clip and ducks the music for its full length.
 *
 * The duck covers every piece, and the clips are trimmed so their length is
 * the length of the speech — the music comes back when the announcer stops
 * rather than after a tail of room tone.
 */
void ReplayVoice_Start(void)
{
    if (s_clipIndex < 0 || s_pieceCount <= 0) {
        return;
    }

    unsigned int now = timeGetTime();
    unsigned int at = now;
    for (int i = 0; i < s_pieceCount; i++) {
        s_pieceStartMs[i] = at;
        at += PieceDurationMs(s_clipIndex, i);
    }
    s_endMs = at;

    PlaySoundEffect(REPLAY_VOICE_SLOT(0), 0, 0);
    s_nextPiece = 1;
    s_playing = 1;

    SFX_DuckMusic((int)(s_endMs - now));
}

/**
 * Starts each remaining piece as its turn comes round. Call once per frame.
 */
void ReplayVoice_Tick(void)
{
    if (!s_playing) {
        return;
    }

    if (g_demoMode != DEMO_REPLAY) {
        ReplayVoice_Stop();
        return;
    }

    unsigned int now = timeGetTime();
    while (s_nextPiece < s_pieceCount &&
           (int)(now + REPLAY_PIECE_LEAD_MS - s_pieceStartMs[s_nextPiece]) >= 0) {
        PlaySoundEffect(REPLAY_VOICE_SLOT(s_nextPiece), 0, 0);
        s_nextPiece++;
    }

    if (s_nextPiece >= s_pieceCount && (int)(now - s_endMs) >= 0) {
        s_playing = 0;
    }
}

/**
 * Silences the sequence and forgets the schedule. The loaded pieces stay in
 * their slots so the same clip can be replayed without reloading.
 */
void ReplayVoice_Stop(void)
{
    for (int i = 0; i < REPLAY_MAX_PIECES; i++) {
        SFX_Stop(REPLAY_VOICE_SLOT(i));
    }
    s_playing = 0;
    s_nextPiece = 0;
}
