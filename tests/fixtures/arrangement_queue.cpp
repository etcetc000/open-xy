#include "arrangement_queue.hpp"
#include "arrangement_registration.hpp"
using namespace opxy;
using namespace opxy::platform;
static void fence() {}
static void require(bool good) { if (!good) __builtin_trap(); }
int main() {
    int occupied; int* slots[256];
    for (u32 n = 0; n < 256; ++n) slots[n] = &occupied;
    require(arrangement_free_page(slots, 256) == 0);
    slots[251] = nullptr;
    require(arrangement_free_page(slots, 256) == 251);
    require(slots[255] == &occupied && slots[251] == nullptr); // read-only selection
    require(arrangement_free_page(slots, 128) == 0);
    require(arrangement_free_page(slots, 257) == 0);
    ArrangementQueue<4> q{};
    ArrangementEvent down{1,2,3,54,1,false,false}, up=down, turn=down, out{};
    up.value=0; turn.turn=true;
    require(q.push(down,false,fence));
    require(q.reserved==1);
    require(q.push(turn,false,fence));
    require(q.push(turn,false,fence));
    require(!q.push(turn,false,fence)); // last slot belongs to release
    require(!q.push(down,false,fence));
    require(q.push(up,true,fence));
    require(q.reserved==0);
    require(q.pop(out,fence) && out.value==1 && !out.turn);
    require(q.pop(out,fence) && out.turn);
    require(q.pop(out,fence) && out.turn);
    require(q.pop(out,fence) && out.value==0);
    require(!q.pop(out,fence));
    q.read=q.written=0xfffffffeu;
    require(q.push(down,false,fence));require(q.push(up,true,fence));
    require(q.pop(out,fence));require(q.pop(out,fence));require(q.read==0);
    ArrangementPublicationSlot snapshot{}; ArrangementPublication view{};
    snapshot.publish({1,2,255,1,1}, fence);
    require(snapshot.read(view,fence) && view.identity==2 && view.page==255);
    bool interrupted = false;
    require(!snapshot.read(view, [&]() { if (!interrupted) { ++snapshot.sequence; interrupted = true; } }));
    snapshot.sequence++;
    snapshot.sequence |= 1;
    require(!snapshot.read(view,fence)); // never spin waiting for preempted GUI
    snapshot.sequence++;
    snapshot.publish({2,3,0,0,0},fence);
    require(snapshot.read(view,fence) && !view.visible && view.generation==2);
    ArrangementMailbox<ArrangementEvent> box{};
    require(box.put(down,fence));
    require(!box.put(up,fence)); // never overwrite outstanding payload
    require(box.take(out,fence) && out.value==1);
    require(!box.take(out,fence));
    require(box.put(up,fence));require(box.take(out,fence) && out.value==0);
}
