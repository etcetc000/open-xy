#include "collection_gate.hpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::collection::activation;
namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
Identity identity() {
    Identity i={}; i.generation=23; i.slot=1;
    for (u32 n=0;n<32;++n) { i.platform[n]=n; i.abi[n]=31-n; i.digest[n]=n^0xa5; }
    return i;
}
Reply accepted_request(StartupGate& gate, u32 op, u32 now=10) {
    const Reply result=gate.request(0x12345678,op,now);
    require(result.status==accepted,"command refused");
    return result;
}
void begin(StartupGate& gate, u32 now=0, u32 timeout=100) {
    require(gate.enter(identity(),true,now,timeout),"enter failed");
    require(!gate.enter(identity(),true,now,timeout),"entered twice");
}
void copying(StartupGate& gate) {
    begin(gate); accepted_request(gate,claim); accepted_request(gate,freeze);
    require(accepted_request(gate,copy).value==copy_authorized,"missing copy authorization");
}
u32 random(u32& seed) { seed=seed*1664525u+1013904223u; return seed; }
}
int main() {
    {
        BootBarrier owner={};
        require(!owner.allow_bulk(false,true),"bulk began without persisted marker");
        require(!owner.allow_commit(true),"commit before bulk admission");
        require(owner.allow_bulk(true,true),"bulk admission failed");
        owner.cancel();
        require(owner.abort_worker() && !owner.return_embedded(),"in-flight bulk released baseline");
        require(!owner.allow_commit(true),"cancelled worker committed");
        require(owner.worker_cleaned() && owner.return_embedded(),"cleaned worker did not release baseline");
        require(!owner.allow_bulk(true,true),"late worker revived cancelled boot");
    }
    {
        BootBarrier owner={};owner.allow_bulk(true,true);owner.cancel();owner.cleanup_timeout();
        require(owner.requires_reset() && !owner.worker_cleaned() && !owner.return_embedded(),"hung bulk released baseline");
    }
    {
        BootBarrier verify={};verify.allow_bulk(true,false);
        require(!verify.allow_commit(true),"verify-only intent granted execution");
        verify.cancel();require(!verify.return_embedded(),"verify retained bulk on release");
        verify.worker_cleaned();require(verify.return_embedded(),"verified cleanup not released");
    }
    for (u32 peer=0;peer<2;++peer) {
        BootBarrier owner={};owner.allow_bulk(true,true);require(owner.allow_commit(true),"commit failed");
        require(owner.adopt(peer)==bool(peer),"publication disagreement ignored");
        if (peer) require(owner.return_selected(),"selected boot not released");
        else require(owner.requires_reset(),"failed publication did not hold");
        owner.cancel();owner.worker_cleaned();owner.cleanup_timeout();
        require(!owner.return_embedded(),"postcommit cancellation returned to overwritten baseline");
    }
    {
        StartupGate gate={};Identity i=identity();i.request_mode=4;
        require(gate.enter_intent(i,true,0,100),"intent admission failed");
        require(!gate.prepared(true),"unclaimed intent prepared");
        require(accepted_request(gate,claim).value==intent_leased,"intent was a copy lease");
        require(accepted_request(gate,request_mode).value==4,"intent mode changed");
        require(gate.request(0x12345678,freeze,10).status==refused,"intent froze before resource verification");
        require(gate.request(0x12345678,copy,10).status==refused,"intent authorized code copy");
        require(gate.prepared(true),"verified intent not prepared");
        accepted_request(gate,freeze);accepted_request(gate,copy);
    }
    {
        StartupGate gate={};gate.enter_intent(identity(),true,0,100);accepted_request(gate,claim);
        require(!gate.prepared(false) && gate.return_embedded(),"failed staging did not cancel intent");
        require(!gate.prepared(true),"cancelled intent revived");
    }
    {
        StartupGate gate={};gate.enter_intent(identity(),true,0,100);accepted_request(gate,claim);
        gate.tick(100);require(gate.return_embedded(),"intent timeout ignored");
        require(!gate.prepared(true),"expired intent revived");
    }
    {
        StartupGate gate={};
        require(gate.request(1,claim,0).status==unavailable,"claim before entry");
        begin(gate);
        require(gate.request(0,claim,0).status==wrong_epoch,"zero epoch");
        require(gate.request(1,freeze,0).status==wrong_epoch,"unclaimed freeze");
        accepted_request(gate,claim);
        for (u32 word=0;word<8;++word) {
            const u32 b=word*4;
            require(accepted_request(gate,platform_word+word).value==
                    (b | (b+1)<<8 | (b+2)<<16 | (b+3)<<24),"platform word changed");
            const Identity i=identity();
            for (u32 op=abi_word;op<=digest_word;op+=0x10) {
                const unsigned char* p=(op==abi_word ? i.abi : i.digest)+b;
                require(accepted_request(gate,op+word).value==
                        (u32(p[0]) | u32(p[1])<<8 | u32(p[2])<<16 | u32(p[3])<<24),"identity word changed");
            }
        }
        require(accepted_request(gate,generation).value==23,"generation changed");
        require(accepted_request(gate,slot).value==1,"slot changed");
        require(gate.request(2,copy,10).status==wrong_epoch,"foreign copy accepted");
        require(gate.request(0x12345678,copy,10).status==refused,"copy before freeze");
        accepted_request(gate,freeze);
        require(gate.request(0x12345678,publish,10).status==refused,"publish before copy");
        accepted_request(gate,copy);
        require(gate.request(0x12345678,publish,10).status==refused,"publish before cache sync");
        require(gate.copied(true),"cache completion rejected");
        require(!gate.copied(false),"late cache result changed completed copy");
        require(accepted_request(gate,copy).value==copy_authorized,"lost copy reply is not idempotent");
        accepted_request(gate,publish);
        require(gate.return_selected() && !gate.return_embedded(),"selected return unavailable");
        accepted_request(gate,publish);
    }
    for (u32 bad=0;bad<4;++bad) {
        StartupGate gate={}; Identity i=identity();
        if (bad==0) i.generation=0;
        if (bad==1) i.slot=2;
        require(!gate.enter(i,bad!=2,0,bad==3 ? 0x80000000u : 100),"bad candidate entered");
        require(gate.return_embedded(),"bad candidate did not select baseline");
    }
    for (u32 action=0;action<3;++action) {
        StartupGate gate={}; begin(gate,0xfffffff0u,100);
        if (action) { accepted_request(gate,claim,0xfffffff0u); }
        if (action==2) accepted_request(gate,freeze,0xfffffff0u);
        gate.tick(83); require(!gate.return_embedded(),"lease expired early at wrap");
        gate.tick(84); require(gate.return_embedded(),"lease did not expire at wrap");
        require(gate.request(0x12345678,copy,85).status==unavailable,"expired lease revived");
    }
    {
        StartupGate gate={}; begin(gate);
        accepted_request(gate,claim,99); accepted_request(gate,freeze,99);
        gate.tick(100);
        require(gate.return_embedded(),"late claim extended the bounded startup deadline");
    }
    {
        StartupGate gate={}; copying(gate);
        require(!gate.copied(false),"cache failure accepted");
        gate.tick(0xffffffffu);
        require(gate.phase()==failed_hold && !gate.return_embedded() && !gate.return_selected(),"postcopy fallback");
    }
    {
        GateMailbox mailbox={}; u32 ticket=0,epoch=0,operation=0;
        require(!mailbox.take(ticket,epoch,operation),"empty mailbox has command");
        require(!mailbox.complete(1,{accepted,123}),"completed empty mailbox");
        require(mailbox.exchange(1,7,claim).status==busy,"new command did not queue");
        for (u32 i=0;i<100;++i) require(mailbox.exchange(i,99,copy).status==busy,"overwrote pending request");
        require(mailbox.take(ticket,epoch,operation) && epoch==7 && operation==claim,"pending command changed");
        require(mailbox.complete(ticket,{accepted,leased}),"completion failed");
        for (u32 i=0;i<100;++i) {
            const Reply r=mailbox.exchange(1,7,claim);
            require(r.status==accepted && r.value==leased,"response not repeatable");
        }
        require(mailbox.exchange(1,7,freeze).status==refused,"changed signature reused ticket");
        require(mailbox.exchange(0,7,claim).status==refused,"zero ticket accepted");
        require(mailbox.exchange(3,7,freeze).status==refused,"skipped ticket accepted");
        require(mailbox.exchange(2,7,freeze).status==busy,"new command used old response");
        require(mailbox.take(ticket,epoch,operation) && operation==freeze,"next command missing");
        require(!mailbox.complete(1,{accepted,published}),"old completion answered new command");
        require(mailbox.complete(ticket,{accepted,frozen}),"current completion rejected");
    }
    {
        StartupGate gate={}; copying(gate);
        GateMailbox mailbox={}; u32 ticket=0,epoch=0,operation=0;
        mailbox.exchange(1,0x12345678,query);
        require(mailbox.take(ticket,epoch,operation),"query not queued");
        mailbox.complete(ticket,gate.request(epoch,operation,10));
        require(mailbox.exchange(1,epoch,query).value==copy_authorized,"initial copy state missing");
        require(gate.copied(true),"copy did not finish");
        require(mailbox.exchange(2,epoch,query).status==busy,"fresh query reused cached receipt");
        require(mailbox.take(ticket,epoch,operation),"fresh query not queued");
        mailbox.complete(ticket,gate.request(epoch,operation,11));
        require(mailbox.exchange(2,epoch,query).value==local_copied,"copy completion not observed");
        require(mailbox.exchange(1,epoch,query).status==refused,"old ticket replayed");
    }
    // Explore stale/duplicate/unknown commands, deadlines and completion order.
    // Once a copy is authorized, every subsequent path must stay out of baseline.
    const u32 operations[]={claim,freeze,copy,publish,cancel,query,0,0xffffffffu,
                            platform_word,abi_word,digest_word,generation,slot};
    u32 seed=0x5a17247;
    for (u32 trial=0;trial<10000;++trial) {
        StartupGate gate={};
        if (trial%2) copying(gate); else begin(gate);
        bool destructive=gate.phase()==copy_authorized;
        u32 now=10;
        for (u32 step=0;step<100;++step) {
            const u32 bits=random(seed);
            if ((bits&31)==0) gate.copied((bits&32)!=0);
            else gate.request((bits&7) ? 0x12345678 : bits,
                              operations[(bits>>8)%(sizeof(operations)/sizeof(*operations))],now);
            if (gate.phase()==copy_authorized || gate.phase()==local_copied ||
                gate.phase()==published || gate.phase()==failed_hold) destructive=true;
            now+=(bits>>24)&15; gate.tick(now);
            require(!destructive || !gate.return_embedded(),"destructive path resumed baseline");
            require(!gate.return_selected() || gate.phase()==published,"uncommitted external return");
        }
    }
    return 0;
}
