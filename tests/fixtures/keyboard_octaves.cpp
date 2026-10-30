#include <cstdio>
#include <cstdlib>
static void host_barrier() {}
#include <keyboard_octaves.cpp>
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
using namespace opxy::platform;
namespace {
i32 stored[8], scale[8];
u32 reads=0, writes=0;
u32 set_octave(u32 track, i32 value) { require(track<8); stored[track]=value; ++writes; return 123; }
i32 effective_octave(u32 track) { require(track<8); ++reads; return stored[track]*scale[track]; }
}
Address opxy::platform::host_stock_address(Address address) {
    if(address==0x8d58573c) return reinterpret_cast<Address>(&set_octave);
    require(address==0x8d58f65c);
    return reinterpret_cast<Address>(&effective_octave);
}
int main() {
    scale[0]=1; scale[1]=2;
    require(sdk_octave_store(0,1)==123 && sdk_track_octaves[0]==1);
    require(sdk_octave_store(1,1)==123 && sdk_track_octaves[1]==2);
    require(sdk_octave_store(1,-1)==123 && sdk_track_octaves[1]==-2);
    require(writes==3 && reads==3);
    // A loaded project can replace the stored value without the octave-key
    // path. Rendering the player refreshes the input split before display.
    stored[0]=-1; sdk_track_octaves[0]=2;
    refresh_keyboard_octave(0); require(sdk_track_octaves[0]==-1);
    // The same stored setting means different pitches after an engine change.
    scale[0]=2; refresh_keyboard_octave(0); require(sdk_track_octaves[0]==-2);
    const u32 before=reads; refresh_keyboard_octave(8); require(reads==before);
    require(sdk_track_octaves[1]==-2);
    std::puts("effective keyboard octave published after edits, project loads and instrument changes");
}
