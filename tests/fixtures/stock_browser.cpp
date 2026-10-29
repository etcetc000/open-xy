// Execute the platform adapter with stock ABI boundary doubles, no device I/O.
#include "stock_browser.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
using namespace opxy::platform::stock_browser;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
static u32 screen_id, browser_storage[40], screen_storage[4], opens, getters;
static detail::Delegate* active;
static char heading_text[16];
static i32 heading_number;
static void* get_browser() { ++getters; return browser_storage; }
static void* get_screen() { return screen_storage; }
static void heading(void* screen, i32 track, const char* title) {
    check(screen==screen_storage && track>=-1 && track<=98,"Stock heading ABI");
    heading_number=track;
    std::strcpy(heading_text,title);
}
static void bank(void* screen, u32 shown) {
    check(screen==screen_storage && shown==0,"No preset-bank editing");
}
static void show(void* object, detail::Catalog* catalog, i32 initial, detail::Delegate* delegate, u32 mode) {
    check(object==browser_storage && mode==0 && catalog->filesystem==0,"Non-filesystem stock browser ABI");
    const u32 count=static_cast<u32>(catalog->rows.end-catalog->rows.begin);
    check(count>0 && initial>=0 && static_cast<u32>(initial)<count && catalog->rows.capacity==catalog->rows.end,
          "Bounded browser selection");
    for(u32 i=0;i<count;++i) {
        const detail::Row& row=catalog->rows.begin[i];
        check(row.name.allocator==0 && row.name.capacity==15 && row.name.length==std::strlen(row.name.text),
              "Stock inline string ABI");
        check(row.id==static_cast<i32>(i) && !row.flags && !row.items.begin && !row.items.end &&
              !row.children.begin && !row.children.end && !row.parent,"Flat private catalog");
    }
    active=delegate; ++opens; screen_id=9;
    detail::Vector<detail::String> options;
    delegate->methods->options(&options,delegate);
    check(!options.allocator && !options.begin && !options.end && !options.capacity,"Hidden vector return ABI");
}
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    switch(address) {
    case kScreen: return reinterpret_cast<Address>(&screen_id);
    case 0x8c09dc28: return reinterpret_cast<Address>(&get_browser);
    case 0x8d550e70: return reinterpret_cast<Address>(&get_screen);
    case 0x8d551c48: return reinterpret_cast<Address>(&heading);
    case 0x8c0a0b90: return reinterpret_cast<Address>(&show);
    case 0x8d5501ec: return reinterpret_cast<Address>(&bank);
    default: check(false,"Unexpected stock call"); return 0;
    }
}
}}
static StockSelector<2> first={};
static StockSelector<3> second={};
static const char* first_labels[2]={"one","two"};
static const char* second_labels[3]={"low","mid","high"};
struct Result { u32 calls,cancels,index; bool try_reentry,reentry; };
static void confirmed(void* context,u32 index) {
    Result& r=*static_cast<Result*>(context); ++r.calls; r.index=index;
    if(r.try_reentry) r.reentry=second.open("other",second_labels,0,Callbacks{context,confirmed,nullptr});
}
static void cancelled(void* context) {
    Result& r=*static_cast<Result*>(context); ++r.cancels;
    if(r.try_reentry) r.reentry=second.open("other",second_labels,0,Callbacks{context,confirmed,nullptr});
}
static void close() {
    detail::Delegate* old=active;
    old->methods->leave(old); active=nullptr; screen_id=0;
    old->methods->leave(old); // Repeated stock notifications must be inert.
}
int main() {
    Result a={},b={};
    Callbacks ca={&a,confirmed,cancelled}, cb={&b,confirmed,cancelled};
    // Reject inputs before calling stock or changing caller storage.
    const char* empty[2]={"one",""};
    const char* long_name[2]={"one","1234567890123456"};
    const char* control[2]={"one","bad\n"};
    const char* null_name[2]={"one",nullptr};
    check(!first.open(nullptr,first_labels,0,ca) && !first.open("",first_labels,0,ca) &&
          !first.open("1234567890123456",first_labels,0,ca),"Invalid titles rejected");
    check(!first.open("pick",empty,0,ca) && !first.open("pick",long_name,0,ca) &&
          !first.open("pick",control,0,ca) && !first.open("pick",null_name,0,ca),"Invalid labels rejected");
    check(!first.open("pick",first_labels,2,ca) && !first.open("pick",first_labels,0,Callbacks{}),
          "Invalid index or missing callback rejected");
    check(getters==0 && opens==0 && !first.rows[0].name.length,"No partial publication of invalid input");
    screen_id=9;
    check(!first.open("pick",first_labels,0,ca) && getters==0,"An unrelated stock browser is not replaced");
    screen_id=0;
    check(first.open("pick",first_labels,1,ca),"First client opens");
    check(std::strcmp(heading_text,"pick")==0 && heading_number==-1 && first.session.candidate==1,
          "Heading and initial selection");
    check(!first.open("again",first_labels,0,ca) && !second.open("other",second_labels,0,cb),"Single modal owner");
    detail::Delegate* stale=active;
    active->methods->highlight(active,0,0);
    active->methods->highlight(active,1,1);
    active->methods->highlight(active,0,-1);
    active->methods->highlight(active,0,99);
    check(a.calls==0 && first.session.candidate==0,"Only valid highlights update pending selection");
    check(active->methods->click(active,1)==1 && a.calls==0,"Unused click is inert");
    a.try_reentry=true;
    check(active->methods->click(active,0)==0,"Stock left click accepts");
    active->methods->highlight(active,0,1);
    check(active->methods->click(active,3)==0 && a.calls==1 && a.index==0 && !a.reentry,
          "Confirm once, freeze accepted index, reject synchronous reentry");
    close(); check(a.cancels==0,"Confirmed close is not cancellation");
    check(second.open("range",second_labels,2,cb),"Second independent client opens");
    stale->methods->highlight(stale,0,0); stale->methods->leave(stale);
    check(second.session.candidate==2 && b.calls==0,"Old client cannot affect new owner");
    active->methods->click(active,3); close();
    check(b.calls==1 && b.index==2 && a.calls==1,"Stock white click uses the second client's context");
    check(first.open("pick",first_labels,1,ca),"Client can reopen");
    active->methods->highlight(active,0,0);
    const u32 m2[1]={7}; check(active->methods->key(active,m2)==1,"M2 consumed as back");
    close(); check(a.cancels==1 && a.calls==1 && !a.reentry,"Cancellation once without confirmation or reentry");
    check(second.open("range",second_labels,0,Callbacks{nullptr,confirmed,nullptr}),"Optional cancel and null context accepted");
    close(); // No confirmation: null context is never dereferenced by this client.
    // Labels and heading are copied, not borrowed from a stack or source buffer.
    char title[16]="temporary",label[16]="copied";
    const char* local[2]={label,"123456789012345"};
    check(first.open(title,local,0,ca),"Fifteen-character inline label accepted");
    title[0]='X'; label[0]='X';
    check(std::strcmp(first.session.title,"temporary")==0 && std::strcmp(first.rows[0].name.text,"copied")==0,
          "Own string storage survives caller edits");
    a.try_reentry=false; close();
    // The first labels only, under a number as the preset browser shows a track's.
    check(!second.open("aux",second_labels,4,0,cb) && !second.open("aux",second_labels,2,0,cb,99) &&
          !second.open("aux",second_labels,2,0,cb,-2) && !second.open("aux",second_labels,2,2,cb,3),
          "Count, number and initial index are bounded");
    check(second.open("aux",second_labels,2,1,cb,3) && heading_number==3 &&
          std::strcmp(heading_text,"aux")==0 && second.session.count==2,"Numbered heading over the first labels");
    close();
    check(opens==6,"Rejected opens never enter stock");
}
