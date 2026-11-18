// Sample Studio's capability (docs/sample-studio.md): one extension per build supplies an editor
// for stock's sample library (main board) and a player beside the master bus (audio board). The
// SDK owns every firmware site and offers the services below; the extension holds no firmware
// address. Experimental and source-level, as the other extension interfaces.
#pragma once
#include <opxy/draw.hpp>
#include <opxy/sample.hpp>
#include <opxy/sample_engine.hpp>
#include <opxy/types.hpp>

namespace opxy { namespace studio {

// ---------------------------------------------------------------- the extension supplies these
// Main board, the library's controller and screen. Each returns whether it took the input; what
// it doesn't take goes to stock's library, unchanged.
struct Editor {
    bool (*press)(u32 key, bool shift);   // a key pressed in the library (codes below)
    bool (*release)(u32 key);
    bool (*turn)(u32 encoder, i32 detents);
    bool (*frame)(i32 elapsed);           // every library frame: true when it drew it (stock_frame() first, if wanted)
};
// Library key codes (press, release).
const u32 key_m1 = 6, key_m2 = 7, key_m3 = 8, key_m4 = 9, key_click0 = 10, key_click3 = 13, key_sample = 23;

// Audio board. No allocation, blocking or file work in any of these: post it (post()).
struct Player {
    void (*receive)(u32 channel, i32 value);  // a message the editor sent (send()), channel 0..14
    void (*mixer_begin)();                    // each block, before the send effects run
    void (*voice)(i32* stereo, u32 track);    // each voice's finished block, before its track sum
    void (*track_gain)(u32 track, i32 gain);  // the gain stock gives a track this block
    void (*track_level)(u32 track, i32 level);// a track's dry level this block
    void (*master)(i32* left, i32* right);    // the master bus after its EQ, before the compressor
};

// ---------------------------------------------------------------- both boards
u32 now_ms();
u32 now_us();
u32 cycles();
void barrier();   // orders the stores before it ahead of those after, across the boards' tasks
// The device log, as stock's own lines ("sample_studio/editor.cpp[line]: ..."), up to twelve 32-bit words.
void log(u32 line, const char* format, i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0, i32 e = 0, i32 f = 0,
         i32 g = 0, i32 h = 0, i32 i = 0, i32 j = 0, i32 k = 0, i32 l = 0);
void log_audio(u32 line, const char* format, i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0, i32 e = 0, i32 f = 0);
// Stock's background worker: job(arg) runs there, then done(arg) back on the posting side (or none).
void post(void (*job)(u32), void (*done)(u32), u32 arg = 0);
// Files on the card, through newlib (worker side only).
typedef void File;
File* file_open(const char* path, const char* mode);
u32 file_read(void* to, u32 size, u32 count, File* file);
u32 file_write(const void* from, u32 size, u32 count, File* file);
void file_close(File* file);
bool rename_file(const char* from, const char* to);
void remove_file(const char* path);
void make_folder(const char* path);
bool file_exists(const char* path);

// ---------------------------------------------------------------- main board
// Memory from stock's heap.
void* allocate(u32 bytes);
void release(void* block);
void popup(const char* text);
// The library.
bool library_on_top();                   // the library is the screen, with nothing over it
bool library_busy();                     // renaming or a delete prompt is up
u32 selected_track();                    // 0..7, or 8 and up for none
u32 sample_engine(u32 track);            // the sample page's engine: sampler, drum, multi or other
const u32 engine_sampler = 2, engine_drum = 3, engine_multisampler = 30;
u32 selected_key(u32 track);             // a drum or multisampler track's selected key (a note)
bool shift_held();
u32 project_bpm_x10();
u32 track_engine(u32 track);             // a track's machine (FX I is track 14, FX II 15)
bool highlighted(char* out, u32 size);   // the library's highlighted file, as a full path
bool highlight_seen();                   // the library has highlighted a file since it opened
void stop_audition();                    // stop the library's own audition of a file
void relist_library();                   // the library lists its folder again (after files change)
void rescan_presets();                   // stock's preset browser learns of new presets
// The track's notes.
u32 note_serial(u32 track);              // counts note starts on the track
bool note_held(u32 track, u32 note);
// A key's sample: stock's region entry, entry_bytes long, read and written in place.
const u32 entry_bytes = 128, path_bytes = 96;
unsigned char* key_entry(u32 track, u32 key);   // or null: no region there
const char* entry_path(const unsigned char* entry);
i32 entry_frames(const unsigned char* entry);
SampleRegion entry_region(const unsigned char* entry);
i32 entry_level(const unsigned char* entry);    // dB, -30..+20
bool entry_reversed(const unsigned char* entry);
u32 entry_root(const unsigned char* entry);     // semitones; 60 is neutral
i32 entry_fine(const unsigned char* entry);
void set_entry_tuning(unsigned char* entry, u32 root, i32 fine);
void set_entry_whole(unsigned char* entry, u32 frames);   // the markers at 0 and frames
bool put_file(u32 track, u32 engine, u32 key, const char* path, u32 frames, u32 root);   // as stock's library loads one
void send_regions(u32 track, bool changed);     // after entries change: stock's table to the voices
void samples_changed();                          // stock gathers the keys' files again
bool samples_gathered();                         // ...and has
// Stock's sample cache.
u32 sample_key(const char* path);
struct Borrowed { const void* object; u32 state; bool unwanted; };   // state: 0 not cached, 1 loading, 2 loaded
Borrowed borrow(u32 key);                        // worker side; release() it
void release_sample(u32 key);
SampleView sample_view(const void* object);
// To the audio board: channel 0..14 (the player's receive()).
void send(u32 channel, i32 value);
// The drawing surface over the library, from the extension's artwork (content/display/sample_studio.svg).
const u32 surface_paths = 32, surface_points = 64, surface_nodes = 96, surface_texts = 32, surface_palette = 8;
typedef draw::Frame<surface_paths, surface_points, surface_nodes, surface_texts, surface_palette> Surface;
// Binds the artwork's IDs: `nodes` node IDs (the first `paths` are its mutable paths), `texts` text
// IDs and `palette` palette IDs, in that order. False until the artwork is loaded.
bool surface_ready(const char* const* ids, u32 paths, u32 nodes, u32 texts, u32 palette);
// Shows the bound slots of `frame`; the rest are ignored and may stay empty. A frame whose bound
// slots are out of range (draw::valid's rules) is not drawn at all.
void surface_present(const Surface& frame);
void surface_blank();                            // the stage's area cleared, while the artwork loads
void stock_frame();                              // stock's library frame, now (inside frame() only)

// ---------------------------------------------------------------- audio board
// The sample pool stock loads samples into.
void* pool_allocate(u32 bytes);
void pool_free(void* block);
u32 pool_in_use();
const u32 pool_capacity = 0x03f00000u, pool_headroom = 8u << 20;
// A cached sample held while it plays (pin), by its key.
const void* pin(u32 key);
void unpin(u32 key);
// The send effects' input buses this block: FX I (0), FX II (1), interleaved stereo Q31.
i32* fx_bus(u32 send);

}}  // namespace opxy::studio
