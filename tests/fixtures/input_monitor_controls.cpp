#include <input_monitor_stock.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
opxy::u32 gui_role = 1;
opxy::u32 stock_gui_role() { return gui_role; }
using namespace opxy;
using namespace opxy::platform;
namespace {
bool capable = true, file_exists, file_failure, close_failure, browser_failure;
u32 shifted, aux_tab, selected_aux, project_id = 1, loading, input_enabled = 1, recorder, source, feedback, active, live_word, io_calls;
extensions::Time now = 10000;
u8 saved[8];
char notification[32];
stock_browser::Callbacks callbacks = {};
u32 initial_choice;
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void* open_file(const char* path, const char* mode) {
    ++io_calls; require(std::strcmp(path,"user/tilt-mic.cfg")==0,"only own preference path may be accessed");
    return file_failure || (*mode=='r' && !file_exists) ? nullptr : saved;
}
u32 read_file(void* out,u32 size,u32 count,void* file) {
    require(file==saved && size*count==8,"bounded preference read");std::memcpy(out,saved,8);return 8;
}
u32 write_file(const void* in,u32 size,u32 count,void* file) {
    require(file==saved && size*count==8,"bounded preference write");std::memcpy(saved,in,8);file_exists=true;return 8;
}
int close_file(void*) { return close_failure ? -1 : 0; }
u32 get_shift() { return shifted; }
u32 get_monitor() { return active; }
void set_monitor(u32 value) { active=value; }
u32 get_source(u32,u32 track,u32 index) { require(track==12 && !index,"read selected input only");return source; }
u32 disabled(u32 selected) { require(!selected,"check internal mic feedback protection");return feedback; }
void live_set(u32 base,u32 track,u32 index,i32 value) {
    require(base==0x86bd027c && track==12 && index==57,"publish only the reserved LIVE marker");live_word=value;
}
}
namespace opxy { namespace platform {
bool extensions_monitor_available() { return capable; }
extensions::Motion extension_motion() { return {0,0,32767,now,true}; }
void extension_notify(const char* text) {
    require(std::strlen(text) < sizeof(notification),"status popup must fit");
    std::strcpy(notification,text);
}
Address host_stock_address(Address address) {
    switch(address) {
    case 0x8d51f184:return reinterpret_cast<Address>(&get_shift);
    case 0x0801eae4:return reinterpret_cast<Address>(&aux_tab);
    case 0x840a0534:return reinterpret_cast<Address>(&selected_aux);
    case 0x840a0524:return reinterpret_cast<Address>(&project_id);
    case 0x840a0528:return reinterpret_cast<Address>(&loading);
    case 0x08023c48:return reinterpret_cast<Address>(&input_enabled);
    case 0x860c072c:case 0x860c072d:return reinterpret_cast<Address>(&recorder);
    case 0x86bd027c + 0x4080 + (12*58+57)*4:return reinterpret_cast<Address>(&live_word);
    case 0x8fed0348:return reinterpret_cast<Address>(&open_file);
    case 0x8fed05bc:return reinterpret_cast<Address>(&read_file);
    case 0x8fed0780:return reinterpret_cast<Address>(&write_file);
    case 0x8fed00ae:return reinterpret_cast<Address>(&close_file);
    case 0x8d4db490:return reinterpret_cast<Address>(&get_monitor);
    case 0x8d4db4a4:return reinterpret_cast<Address>(&set_monitor);
    case 0x8d5ac910:return reinterpret_cast<Address>(&get_source);
    case 0x8d4dc450:return reinterpret_cast<Address>(&disabled);
    case 0x8d5acea8:return reinterpret_cast<Address>(&live_set);
    default:std::abort();
    }
}
namespace stock_browser { namespace detail {
bool open(Session&,Row*,u32 count,const char* title,const char* const* labels,u32 initial,Callbacks cb,i32) {
    require(count==2 && std::strcmp(title,"tilt mic")==0 && std::strcmp(labels[0],"off")==0 && std::strcmp(labels[1],"auto")==0,"stock selector has explicit Off and Auto");
    initial_choice=initial;callbacks=cb;return !browser_failure;
}
}}
}}
int main() {
    gui_role=0; sdk_input_monitor_startup();
    require(!io_calls,"audio startup must perform no file I/O");
    gui_role=1; sdk_input_monitor_startup();
    require(!extension_input_monitor().available,"missing preference must default Off");
    capable=false;require(!sdk_input_monitor_menu(),"no permission owner means no menu");capable=true;
    require(sdk_input_monitor_menu() && initial_choice==0,"menu must show current Off preference");
    require(!extension_input_monitor().available,"opening without confirming must not change preference");
    file_failure=true;callbacks.confirm(nullptr,1);
    require(!extension_input_monitor().available && std::strcmp(notification,"tilt mic save failed")==0,"failed save must not enable auto");
    file_failure=false;callbacks.confirm(nullptr,1);
    require(extension_input_monitor().available,"confirmed Auto must enable eligibility");
    input_monitor::automatic=0;sdk_input_monitor_startup();
    require(extension_input_monitor().available,"Auto preference must survive restart");
    selected_aux=12;
    static i32 turn[3] = {1,0,0};
    require(!sdk_input_monitor_turn(0,turn),"unshifted input turn must pass through");
    shifted=1; aux_tab=1;
    require(!sdk_input_monitor_shift_visible(),"other Aux tabs must keep stock Shift controls");
    aux_tab=0;
    require(sdk_input_monitor_shift_visible(),"Shift must reveal control on external input page");
    for (u32 encoder=1;encoder<4;++encoder)
        require(!sdk_input_monitor_turn(encoder,turn),"other encoders must retain stock routing");
    turn[0]=-1;
    require(sdk_input_monitor_turn(4,turn) && !input_monitor::automatic,"dark encoder counterclockwise selects Off");
    const u32 saved_io=io_calls;
    sdk_input_monitor_turn(0,turn);
    require(io_calls==saved_io,"turns at Off endpoint must not rewrite preference");
    turn[0]=1;file_failure=true;sdk_input_monitor_turn(0,turn);
    require(!input_monitor::automatic,"failed encoder save must retain previous mode");
    file_failure=false;sdk_input_monitor_turn(0,turn);
    require(input_monitor::automatic,"clockwise selects saved Auto");
    shifted=0;
    const u32 io_before=io_calls;
    require(extension_monitor_input(true) && active && (live_word&1u),"Auto acquires and publishes fade-in");
    require(std::strcmp(notification,"tilt mic live")==0,"Aux track must confirm live monitoring");
    require(extension_monitor_input(false) && active && !(live_word&1u),"release must retain monitoring through fade-out");
    now+=100000;require(extension_monitor_input(true) && active && (live_word&1u),"renew during release must reverse fade");
    require(extension_monitor_input(false),"new release starts another deadline");
    now+=input_monitor::release_us;
    require(extension_monitor_input(false) && !active && !(live_word&1u) && live_word!=input_monitor::released,
            "clear the monitor flag before relinquishing the envelope token");
    require(extension_monitor_input(false) && live_word==input_monitor::released,
            "next controller update may release the token after stock publishes the flag");
    require(io_calls==io_before,"controller/motion processing must perform no file I/O");
    require(extension_monitor_input(true),"second lease must acquire");
    active=0;require(!extension_monitor_input(true) && !extension_input_monitor().owned,"manual stop must cancel ownership");
    require(!extension_monitor_input(true) && live_word==input_monitor::released,"manual stop remains blocked and clears its token after stock publication");
    extension_monitor_input(false);active=1;
    require(!extension_monitor_input(true),"manual monitoring must never be borrowed");
    callbacks.confirm(nullptr,0);extension_monitor_input(false);
    require(active && !extension_input_monitor().available,"disabling Auto must preserve manual monitoring");
    input_monitor::automatic=1;sdk_input_monitor_startup();
    require(!input_monitor::automatic,"Off preference must survive restart");
    saved[5]=255;input_monitor::automatic=0;sdk_input_monitor_startup();
    require(!input_monitor::automatic,"malformed preference must not enable auto");
    browser_failure=true;require(!sdk_input_monitor_menu(),"rejected browser open must be reported");
    return 0;
}
