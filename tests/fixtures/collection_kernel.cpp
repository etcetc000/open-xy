// Deterministic RTOS model: schedule the worker at delay boundaries, while
// exercising the actual resident initializer and RPC dispatcher together.
#include <collection_kernel.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
struct Held {};
std::string mode;
u32 tick,steps,driver_ticket=1,command_index;
bool gui_role=true,saw_cleaning;
struct Command { u32 operation,status,value; };
std::vector<Command> commands;
void require(bool good,const char* text) { if (!good) throw std::runtime_error(text); }
void ready_resources() {
    static resources::Entry entry;
    image.status=a::staged;image.version=2;image.resources_count=1;
    cache.verified=true;cache.entries=&entry;cache.count=1;
}
a::Reply rpc(u32 ticket,u32 operation,u32 epoch=17,u32 marker=0) {
    alignas(4) unsigned char message[36]={};message[8]=45;
    *reinterpret_cast<u32*>(message+12)=magic;*reinterpret_cast<u32*>(message+28)=epoch;
    *reinterpret_cast<u16*>(message+32)=u16(marker ? marker : 0x8000u | ticket<<7 | operation);
    sdk_collection_rpc_dispatch(message);
    return a::Reply{*reinterpret_cast<u32*>(message+20),*reinterpret_cast<u32*>(message+24)};
}
void schedule(u32) {
    ++tick;++steps;require(steps<80000,"initializer never resolved");
    if (abort_worker) {
        if (!barrier.return_embedded()) {
            require(!sdk_collection_dispatch_enabled,"baseline resumed before worker cleanup");saw_cleaning=true;
        }
        if (mode=="bulk-stall" || mode=="commit-failure") {
            if (barrier.requires_reset()) throw Held{};
        } else worker_state=cleaned;
    }
    if (gui_role) {
        if (permit==bulk_permit && worker_state==pending_ready) {
            if (mode=="precommit-failure") worker_state=failed;
            else if (mode=="bulk-stall") worker_state=preparing;
            else if (mode=="verify-only") worker_state=finished;
            else { ready_resources();worker_state=commit_wait; }
        }
        if (permit==commit_permit && worker_state==commit_wait)
            worker_state=mode=="commit-failure" ? failed : publish_ready;
        return;
    }
    if (permit==bulk_permit && worker_state==intent_ready) { ready_resources();worker_state=prepared_state; }
    if (permit==copy_permit && worker_state==prepared_state) worker_state=copied_state;
    if (!abort_worker && command_index<commands.size()) {
        const Command& command=commands[command_index];const a::Reply reply=rpc(driver_ticket,command.operation);
        if (reply.status!=a::busy) {
            require(reply.status==command.status && reply.value==command.value,"unexpected processor reply");
            ++command_index;++driver_ticket;
        }
    }
}
u32 role() { return gui_role; }
u32 now() { return tick; }
void* create(void(*)(void*),const char*,u32 words,void*,u32 priority,u32*,void*) {
    require(words==2048 && priority==1,"wrong persistent helper task geometry");
    if (mode=="create-failure") return nullptr;
    if (gui_role) {
        selection.phase=mode=="verify-only" ? a::verify_only : a::armed;
        worker_state=mode=="ineligible" ? ineligible : mode=="small-timeout" ? reading : pending_ready;
    } else {
        selection.phase=mode=="audio-verify" ? a::verify_only : a::armed;
        identity.generation=1;identity.slot=0;identity.request_mode=selection.phase;worker_state=intent_ready;
    }
    return reinterpret_cast<void*>(1);
}
u32 send_reply(u32,void*) { return 0; }
}
volatile u32 sdk_collection_dispatch_enabled;
void sdk_collection_fence() {}
u32 sdk_collection_sync_code(u32,u32) { throw std::runtime_error("initializer copied code directly"); }
u32 sdk_collection_cache_scratch() { throw std::runtime_error("initializer ran cache test directly"); }
u32 sdk_collection_rpc_stock(void*) { throw std::runtime_error("unexpected stock dispatcher path"); }
extern "C" {
u32 sdk_collection_try_lock(volatile u32*) __asm__("sdk_collection_try_lock");
void sdk_collection_unlock(volatile u32*) __asm__("sdk_collection_unlock");
}
u32 sdk_collection_try_lock(volatile u32*) { throw std::runtime_error("initializer parsed resources directly"); }
void sdk_collection_unlock(volatile u32*) { throw std::runtime_error("initializer parsed resources directly"); }
opxy::platform::Address opxy::platform::host_stock_address(u32 address) {
    switch (address) {
    case 0x8d5c3eac:return reinterpret_cast<Address>(role);
    case 0x8c03ee20:return reinterpret_cast<Address>(now);
    case 0x8c040188:return reinterpret_cast<Address>(schedule);
    case 0x8c03f744:return reinterpret_cast<Address>(create);
    case 0x8c0c0c80:return reinterpret_cast<Address>(send_reply);
    default:throw std::runtime_error("initializer performed unexpected stock operation");
    }
}
u32 opxy::platform::word(u32 address) {
    require(address==0x84259220,"initializer performed unexpected stock memory read");return 1;
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    mode=argv[1];gui_role=mode.substr(0,6)!="audio-";
    if (!gui_role) {
        commands={{a::claim,a::accepted,a::intent_leased},{prepare_bulk,a::accepted,a::intent_leased},
                  {wait_prepared,a::accepted,a::leased},{a::freeze,a::accepted,a::frozen}};
        if (mode=="audio-verify") {
            commands.push_back({a::copy,a::refused,a::frozen});commands.push_back({a::cancel,a::accepted,a::baseline});
        } else {
            commands.push_back({a::copy,a::accepted,a::copy_authorized});
            commands.push_back({wait_copied,a::accepted,a::local_copied});commands.push_back({a::publish,a::accepted,a::published});
        }
    }
    bool held=false;
    try { sdk_collection_rpc_entry(); } catch (const Held&) { held=true; }
    if (mode=="bulk-stall" || mode=="commit-failure") {
        require(held && !sdk_collection_dispatch_enabled && !entry_finished && barrier.requires_reset(),"unsafe fallback after unresolved worker/commit");
    } else {
        require(!held && sdk_collection_dispatch_enabled && entry_finished,"embedded or selected boot did not resume");
        const bool expected=mode=="publish" || mode=="audio-publish";
        require(bool(selected)==expected,"wrong collection selected");
        if (mode=="precommit-failure" || mode=="verify-only" || mode=="audio-verify")
            require(saw_cleaning,"bulk failure skipped cleanup ownership");
        if (mode=="audio-publish") {
            const Command& last=commands.back();
            const a::Reply receipt=rpc(driver_ticket,last.operation);
            require(receipt.status==a::accepted && receipt.value==a::published,"publication receipt lost after initializer return");
            require(rpc(driver_ticket+1,a::query).status==a::unavailable,"new command queued after initializer return");
            startup_bits=3;
            require(rpc(0,0,17,live_marker).value==0x103,"live health missing selected/initialized bits");
            require(rpc(0,0,18,live_marker).status==a::unavailable,"stale epoch admitted to live health");
            resource_fault=1;
            require(rpc(0,0,17,live_marker).value==3,"resource fault admitted to health");
        }
        const u32 before=steps;sdk_collection_rpc_entry();require(before==steps,"reinitialization restarted loader");
    }
    return 0;
}
