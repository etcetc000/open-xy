#include "../../sdk/platform/opxy-1.2.1/arranger_song_import.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
static void require(bool ok, const char* why) { if (!ok) { std::puts(why); std::exit(1); } }
static u8 project[0x5401bc + 14 * 0x68], before[sizeof(project)];
static arranger::Model model, scratch;
static u8* source(u32 track, u32 pattern = 0) { return project + 0xd7c + track * 0x53f44 + pattern * 0x53f4; }
static u8* scene(u32 id) { return project + 0x98 + id * 33; }
static u8* order() { return project + 0x5401bc; }
static void word(u8* p, u32 x) { for (u32 n=0; n<4; ++n) p[n] = static_cast<u8>(x >> (n*8)); }
static void reset() {
    std::memset(project, 0, sizeof(project)); model = arranger::Model();
    for (u32 track=0; track<16; ++track) {
        word(source(track)+0x53f40, 2);
        for (u32 pattern=0; pattern<2; ++pattern) { source(track,pattern)[0]=16; source(track,pattern)[8]=3; }
    }
    scene(0)[32]=scene(1)[32]=1;
    word(order()+0x60,1);
}
int main() {
    reset();
    source(0)[0]=5; source(0)[8]=5; // ten steps; scene duration comes from AUX track.
    source(8)[0]=32;
    order()[0]=0; order()[1]=0; order()[2]=1; word(order()+0x60,3);
    scene(1)[0]=1; scene(1)[17]=2;
    std::memcpy(before, project, sizeof(project));
    require(arranger_import_song(model,project,0,100,scratch)==SongImportResult::Ok,"import scenes");
    require(model.clip(0)->start==100 && model.clip(0)->length==64*480 && model.clip(0)->source_length==5*480,"repeat run continuous and AUX controls duration");
    require(model.clip(8)->pattern==1 && model.clip(8)->start==100+64*480 && model.clip(8)->offset==0,"different scene restarts B");
    require(!model.clip(15),"muted lane emitted as gap");
    require(std::memcmp(before,project,sizeof(project))==0,"project sources/order/mutes untouched");
    const arranger::Model saved=model;
    require(arranger_import_song(model,project,0,100,scratch)==SongImportResult::NoRoom,"collision rejects whole import");
    require(std::memcmp(&saved,&model,sizeof(model))==0,"collision preserves prior arrangement");
    reset(); project[8]=1; source(0)[0]=3; source(0)[8]=0;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Ok && model.clip(0)->length==480,"shortest duration scale conversion");
    reset(); project[8]=2; project[31]=1; source(0)[0]=25;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Ok && model.clip(0)->length==16*480,"time signature floors longest");
    reset(); project[8]=2; project[31]=0;
    for (u32 t=0;t<16;++t) source(t)[0]=1;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Ok && model.clip(0)->length==12*480,"time signature minimum unit");
    reset(); source(15)[8]=15;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Ok && model.clip(0)->length==16*480*32,"native duration scale15 exact table");
    reset(); source(0)[8]=12;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::UnsupportedState && !model.clip(0),"1/9 source requires exact phase model");
    reset(); source(0)[8]=15;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::UnsupportedState && !model.clip(0),"1/32 source requires exact phase model");
    reset(); word(order()+0x60,96);
    for(u32 n=0;n<96;++n) order()[n]=n%2;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::NoRoom && !model.clip(0),"capacity overflow atomic");
    reset(); order()[1]=100; word(order()+0x60,2);
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Invalid && !model.clip(0),"invalid late scene atomic");
    reset(); scene(1)[16]=1; order()[0]=1;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::UnsupportedState,"unknown transient state rejected");
    reset(); scene(0)[16]=2;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::CurrentSceneMuted && !model.clip(0),"current native mute must not silence imported clips");
    reset(); project[6]=100;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Invalid,"current scene bounds");
    reset(); scene(0)[32]=0;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::UnsupportedState,"unused scene not invented");
    reset(); word(order()+0x60,97);
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Invalid,"order capacity guard");
    reset(); source(9)[8]=16;
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Invalid,"AUX scale checked");
    reset(); word(source(0)+0x53f40,17);
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Invalid,"source bank checked");
    reset();
    require(arranger_import_song(model,project,0,0xfffffff0u,scratch)==SongImportResult::NoRoom,"time overflow checked");
    require(arranger_import_song(model,project,14,0,scratch)==SongImportResult::Invalid,"Song index checked");
    require(arranger_import_song(model,project,0,0,model)==SongImportResult::Invalid,"scratch cannot alias output");
    word(order()+0x60,0);
    require(arranger_import_song(model,project,0,0,scratch)==SongImportResult::Empty,"empty Song leaves model alone");
    return 0;
}
