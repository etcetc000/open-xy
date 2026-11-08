#include <input_monitor_ui.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy::platform::input_monitor_popup;
namespace {
bool held, ready, extra_available;
u32 tuning;
u32 choice, constructs, loads, binds, resets, frames, draws;
Node value_nodes[4], row_nodes[4], icon_nodes[4];
char captions[4][32];
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
void construct(Popup* p,u32 page,const char* asset) {
    require(!page && std::strcmp(asset,"fx_send.svg")==0,"use a private unregistered stock send presenter");
    ++constructs; p->page=page;p->asset=asset;
}
void load(Popup* p) { ++loads; if(ready)p->svg=reinterpret_cast<void*>(1); }
void bind(Popup* p) {
    require(p->svg!=nullptr,"bind only a loaded document");++binds;
    for(u32 i=0;i<4;++i){p->values[i]=value_nodes+i;p->rows[i]=row_nodes+i;p->icons[i]=icon_nodes+i;}
}
void reset(Popup* p) { ++resets;p->motion[0]=0xc1c00000u; }
void advance(Popup* p,i32) {
    require(!p->svg,"stock updates and dims before the final customized draw");++frames;
    // The native presenter owns its easing. Return its first eased position,
    // then its rest state, so the adapter must preserve the supplied motion.
    p->motion[0]=p->motion[0]==0xc1c00000u?0xc1666667u:0;
    for(u32 i=0;i<4;++i){p->rows[i]->visible=1;p->icons[i]->visible=1;p->rows[i]->matrix[0]=p->rows[i]->matrix[4]=p->rows[i]->matrix[8]=1024;}
}
void matrix(void* raw,const i32* m) { std::memcpy(static_cast<Node*>(raw)->matrix,m,9*sizeof(i32)); }
void text(void* node,const char* value) {
    const auto row = static_cast<Node*>(node) - value_nodes;
    require(row >= 0 && row < 4 && std::strlen(value) < sizeof(captions[0]), "bounded private caption");
    std::strcpy(captions[row],value);
}
void draw(Popup* p,u32 flags) {
    require(p->svg==reinterpret_cast<void*>(1) && !flags,"restore the document before drawing");++draws;
    require(p->rows[0]->visible,"first stock container remains visible");
    for(u32 i=0;i<4;++i){require(!p->icons[i]->visible,"hide send-specific icons");if(i)require(p->rows[i]->visible==(tuning != 0),"tuning rows follow the installed controls");}
}
}
bool sdk_input_monitor_shift_visible() { return held; }
u32 sdk_live_input_page() { return held ? tuning : 0; }
const char* sdk_live_input_page_hint() {
    return extra_available ? (tuning == 2 ? "m1 > quick" : "m1 > vocal fx") : nullptr;
}
const char* sdk_live_input_caption(u32 row, bool label) {
    const char* labels[] = {nullptr, "tune", "retune", "amount"};
    const char* values[] = {nullptr, "F# minor", "hard", "100%"};
    const char* extra_labels[] = {"harmony", "blend", "compress", "de-ess"};
    const char* extra_values[] = {"third up", "20%", "soft", "off"};
    if (tuning == 2) return label ? extra_labels[row] : extra_values[row];
    return label ? labels[row] : values[row];
}
u32 sdk_input_monitor_choice() { return choice; }
namespace opxy { namespace platform {
Address host_stock_address(Address a) {
    switch(a){
    case 0x8d520844:return reinterpret_cast<Address>(&construct);
    case 0x8d520ce0:return reinterpret_cast<Address>(&load);
    case 0x8d53b04c:return reinterpret_cast<Address>(&bind);
    case 0x8d53adc4:return reinterpret_cast<Address>(&reset);
    case 0x8d53ae00:return reinterpret_cast<Address>(&advance);
    case 0x8d5f06a6:return reinterpret_cast<Address>(&matrix);
    case 0x8d6011f0:return reinterpret_cast<Address>(&text);
    case 0x8d51fe1c:return reinterpret_cast<Address>(&draw);
    default:std::abort();
    }
}
}}
int main() {
    sdk_input_monitor_frame(63,63);require(!constructs,"Off-page/Shift-free frames do no popup work");
    held=true;sdk_input_monitor_frame(0,63);require(!constructs,"first slot observes visibility without drawing");
    sdk_input_monitor_frame(63,63);require(constructs==1 && loads==1 && !binds && !draws,"defer until stock artwork loads");
    ready=true;choice=1;sdk_input_monitor_frame(63,63);
    require(constructs==1 && binds==1 && resets==1 && frames==1 && draws==1,"native presenter initializes and advances once");
    require(std::strcmp(captions[0],"tilt mic   auto")==0,"Auto caption");
    require(row_nodes[0].matrix[7]==46695,"center the row while retaining native -14.4 px entrance motion");
    require(value_nodes[0].matrix[6]==155*1024 && value_nodes[0].matrix[7]==57*1024+512,"caption stays inside the stock container");
    choice=0;sdk_input_monitor_frame(63,63);
    require(resets==1 && draws==2 && row_nodes[0].matrix[7]==60*1024,"steady frames retain motion and settle at center");
    require(std::strcmp(captions[0],"tilt mic   off")==0,"Off caption");
    held=false;sdk_input_monitor_frame(0,63);require(draws==2,"release removes the popup");
    held=true;sdk_input_monitor_frame(63,63);require(resets==2,"reopening restarts the same native animation");
    sdk_input_monitor_frame(0,21);sdk_input_monitor_frame(63,63);require(resets==3,"leaving and returning resets animation");
    tuning=1;sdk_input_monitor_frame(63,63);
    require(row_nodes[0].matrix[7]==0 && row_nodes[3].matrix[7]==0,"full overlay occupies the stock four rows");
    require(std::strcmp(captions[1],"tune   F# minor")==0 && std::strcmp(captions[2],"retune   hard")==0 &&
            std::strcmp(captions[3],"amount   100%")==0,"tune values map to the three remaining encoders");
    require(value_nodes[3].matrix[7]==177*1024+512,"last caption fits inside its stock row");
    extra_available=true;sdk_input_monitor_frame(63,63);
    require(std::strcmp(captions[3],"amount   100%   m1 > vocal fx")==0 &&
            value_nodes[3].matrix[0]==717,"quick page hints at vocal effects in the last row");
    tuning=2;sdk_input_monitor_frame(63,63);
    require(std::strcmp(captions[0],"harmony   third up")==0 && std::strcmp(captions[1],"blend   20%")==0 &&
            std::strcmp(captions[2],"compress   soft")==0 &&
            std::strcmp(captions[3],"de-ess   off   m1 > quick")==0,
            "the second page uses all four rows, including the Tilt Mic row");
    require(motion_q10(0x41c00000u)==24*1024 && motion_q10(0xc1c00000u)==-24*1024,"native slide endpoints");
    require(!motion_q10(0x7fc00000u) && !motion_q10(1),"invalid or subpixel motion is bounded");
    return 0;
}
