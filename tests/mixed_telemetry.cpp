#define main original_protocol_main
#include "telemetry_protocol.cpp"
#undef main

struct MixedRegistry {
    Producer<256> mixed{Mode::Packed16AndSnapshot,true};
    Producer<1> other;
    static Bank* resolve(void* p,u16 engine,u8 track) {
        if(track) return 0;
        auto& r=*static_cast<MixedRegistry*>(p);
        return engine==5?&r.mixed.bank:engine==6?&r.other.bank:0;
    }
};
void mixed_fragment_reset_and_loss() {
    MixedRegistry registry;CriticalState lock;
    AudioEndpoint audio{Critical{&lock,enter,leave},MixedRegistry::resolve,&registry};
    GuiEndpoint gui;Pipe up,down;u32 out[256]={};
    CHECK(gui.select({5,0,1},256,Mode::Packed16AndSnapshot,true));
    CHECK(gui.service(0,Pipe::send,&up));auto request=up.pop();CHECK(audio.receive(request.bytes,request.size,0));
    auto& bank=registry.mixed.bank;bank.begin_update();bank.add(0,0x00020064);
    for(unsigned i=1;i<256;++i)bank.publish(i,1000+i);
    CHECK(!audio.service(0,Pipe::send,&down));bank.end_update();
    std::vector<Packet> fragments;
    for(unsigned t=1;t<=5;++t){CHECK(audio.service(t,Pipe::send,&down));fragments.push_back(down.pop());}
    CHECK(registry.mixed.storage[0]==0 && registry.mixed.storage[255]==1255);
    bank.add(0,0x00030014);for(unsigned i=1;i<256;++i)bank.publish(i,2000+i);
    for(int i=4;i>=1;--i){CHECK(gui.receive(fragments[i].bytes,fragments[i].size));CHECK(!gui.take(out,256));}
    CHECK(gui.receive(fragments[0].bytes,fragments[0].size));CHECK(gui.take(out,256)==256);
    CHECK(out[0]==0x00020064 && out[255]==1255);
    CHECK(gui.take(out,256)==256 && out[0]==0 && out[255]==1255);
    CHECK(gui.receive(fragments[0].bytes,fragments[0].size));CHECK(gui.take(out,256)==256 && out[0]==0);
    // Dropped explicit ACK, then page switch: persistent receipt must prevent replay.
    up.drop=true;CHECK(gui.service(6,Pipe::send,&up));up.drop=false;
    CHECK(gui.select({6,0,2},1,Mode::Persistent));CHECK(gui.service(7,Pipe::send,&up));request=up.pop();
    CHECK(audio.receive(request.bytes,request.size,7));CHECK(registry.mixed.storage[0]==0x00030014);
    CHECK(gui.select({5,0,3},256,Mode::Packed16AndSnapshot,true));CHECK(gui.service(8,Pipe::send,&up));request=up.pop();
    CHECK(audio.receive(request.bytes,request.size,8));
    for(unsigned t=8;t<13;++t){CHECK(audio.service(t,Pipe::send,&down));auto p=down.pop();CHECK(gui.receive(p.bytes,p.size));}
    CHECK(gui.take(out,256)==256 && out[0]==0x00030014 && out[255]==2255);
    CHECK(gui.service(13,Pipe::send,&up));auto ack=up.pop();CHECK(audio.receive(ack.bytes,ack.size,13));
    const u32 command=gui.request_reset();CHECK(command);CHECK(gui.service(14,Pipe::send,&up));request=up.pop();
    CHECK(audio.receive(request.bytes,request.size,14));CHECK(bank.pending_command()==command);
    bank.begin_update();bank.publish(255,0);bank.add(0,0x0004001e);bank.command_applied(command);bank.end_update();
    for(unsigned t=29;t<34;++t){CHECK(audio.service(t,Pipe::send,&down));auto p=down.pop();CHECK(gui.receive(p.bytes,p.size));}
    CHECK(gui.take(out,256)==256 && out[0]==0x0004001e && out[255]==0 && gui.applied_command()==command);
    CHECK(!lock.depth);
}
void old_packet_rejected_before_selection_receipt() {
    Fixture f;f.select(1,1,Mode::AccumulatePacked16);f.registry.accumulator.bank.add(0,100);
    Packet old=f.publish(0);f.registry.accumulator.bank.add(0,20);
    // Native capture now installs the new key before draining its local inbox.
    CHECK(f.gui.select({2,0,2},16,Mode::Persistent));CHECK(!f.gui.receive(old.bytes,old.size));
    CHECK(f.gui.service(1,Pipe::send,&f.up));Packet request=f.up.pop();
    CHECK(f.audio.receive(request.bytes,request.size,1));CHECK(f.registry.accumulator.storage[0]==120);
}
void receipt_requires_consumer_take() {
    Fixture f;f.select(1,1,Mode::AccumulatePacked16);f.registry.accumulator.bank.add(0,100);
    auto pending=f.publish(0);f.receive(pending);f.registry.accumulator.bank.add(0,20);
    CHECK(!f.gui.service(1,Pipe::send,&f.up)); // no premature ACK
    f.receive(pending);CHECK(!f.gui.service(2,Pipe::send,&f.up));
    f.select(2,16,Mode::Persistent,2,false,3);CHECK(f.registry.accumulator.storage[0]==120);
    f.select(1,1,Mode::AccumulatePacked16,3,false,4);auto restored=f.publish(4);f.receive(restored);
    u32 out[1];CHECK(f.gui.take(out,1)==1 && out[0]==120);f.ack(5);
    CHECK(!f.gui.take(out,1));
}
struct SlotRegistry {
    struct Slots : Producer<60> { Slots() : Producer<60>(Mode::Packed16AndSnapshot,true,24) {} } tracks[2];
    static Bank* resolve(void* p,u16 engine,u8 track) {
        return engine==7 && track<2 ? &static_cast<SlotRegistry*>(p)->tracks[track].bank : 0;
    }
};
void independent_slots_migration_and_targeted_reset() {
    SlotRegistry r;CriticalState lock;AudioEndpoint audio{Critical{&lock,enter,leave},SlotRegistry::resolve,&r};
    GuiEndpoint gui;Pipe up,down;u32 out[60]={};
    auto request=[&](u32 now) { CHECK(gui.service(now,Pipe::send,&up));auto p=up.pop();CHECK(audio.receive(p.bytes,p.size,now));return p; };
    auto snapshot=[&](u32 now) { for(unsigned i=0;i<2;++i){CHECK(audio.service(now+i,Pipe::send,&down));auto p=down.pop();CHECK(gui.receive(p.bytes,p.size));} };
    CHECK(gui.select({7,0,1},60,Mode::Packed16AndSnapshot,true,24));request(0);
    auto& a=r.tracks[0].bank;auto& b=r.tracks[1].bank;
    a.add(2,100);a.add(9,700);a.publish(24,2);a.publish(25,101);a.publish(59,12345);
    snapshot(0);a.add(2,20); // received but unread A flight
    // Global slot2 now belongs to trackB/note202. Its meter lane stays separate.
    b.add(2,300);b.publish(24,2);b.publish(25,202);b.publish(59,54321);
    CHECK(gui.select({7,1,2},60,Mode::Packed16AndSnapshot,true,24));request(2);
    CHECK(r.tracks[0].storage[2]==120 && r.tracks[0].storage[9]==700);
    snapshot(2);CHECK(gui.take(out,60)==60 && out[2]==300 && out[9]==0 && out[25]==202 && out[59]==54321);request(4);
    const u32 command=gui.request_reset(202);CHECK(command);auto reset=request(5);
    CHECK(reset.size==34 && b.pending_command()==command && b.pending_target()==202);
    auto conflict=reset;conflict.bytes[30]=203;CHECK(!audio.receive(conflict.bytes,conflict.size,6));
    CHECK(b.pending_target()==202);
    // Replayed command cannot change the target; a replacement note303 is safe.
    u32 current_token=303,cursor=48;
    if(b.pending_target()==current_token)cursor=0;
    b.command_applied(command);CHECK(cursor==48 && !b.pending_command());
    CHECK(audio.receive(reset.bytes,reset.size,7));CHECK(!b.pending_command());
    // Returning to A restores its independent history exactly once.
    CHECK(gui.select({7,0,3},60,Mode::Packed16AndSnapshot,true,24));request(8);snapshot(8);
    CHECK(gui.take(out,60)==60 && out[2]==120 && out[9]==700 && out[25]==101 && out[59]==12345);
    CHECK(gui.take(out,60)==60 && out[2]==0 && out[9]==0 && out[59]==12345);request(10);
    // Prefix disagreements cannot turn persistent wave samples into accumulators.
    Header h={Kind::Request,Mode::Packed16AndSnapshot,{7,0,4},0,0,60,0,0};Packet bad;
    CHECK(encode(h,0,bad,23));CHECK(!audio.receive(bad.bytes,bad.size,11));
    CHECK(!encode(h,0,bad,25)); // >24 is not a supported lane count
    h.command=1;CHECK(!encode(h,0,bad,24,0));
    CHECK(encode(h,0,bad,24,0xffffffffu));Header parsed;
    CHECK(!decode(bad.bytes,bad.size-1,parsed));CHECK(!decode(bad.bytes,bad.size+1,parsed));
    CHECK(!lock.depth);
}
void legacy_wire_bytes_unchanged() {
    for(Mode mode:{Mode::Persistent,Mode::AccumulatePacked16}) {
        Header h={Kind::Request,mode,{1,0,1},0,0,1,0,0};Packet p;
        CHECK(encode(h,0,p));CHECK(p.size==30 && p.bytes[29]==0);
        const u8 expected[]={4,'F','X','T','M',2,1,u8(mode),1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0};
        for(unsigned i=0;i<30;++i)CHECK(p.bytes[i]==expected[i]);
    }
}
int main(){original_protocol_main();mixed_fragment_reset_and_loss();old_packet_rejected_before_selection_receipt();receipt_requires_consumer_take();independent_slots_migration_and_targeted_reset();legacy_wire_bytes_unchanged();
    std::puts("mixed telemetry: fragmented snapshot, consume-once prefix, reset and selection boundary passed");}
