// Included by track_control.cpp: same authenticated 1.2.1 newlib calls used by
// project_companion.cpp. No new firmware hooks. Only explicit GUI operations.
#include <opxy/morph_preset_files.hpp>
namespace {
bool morph_preset_path(char* path,u32 slot,u32 copy) {
 if(slot>=16u || copy>=2u || !morph_unique_owner() || !morph_live_context())return false;
 const char* source="/fat32/morph-ab-01a.mab";
 for(u32 n=0;n<24u;++n)path[n]=source[n];
 const u32 number=slot+1u;path[16]=char('0'+number/10);path[17]=char('0'+number%10);path[18]=char('a'+copy);
 return true;
}
}
extern "C" opxy::u32 sdk_morph_preset_read(u32 slot,u32 copy,opxy::u8* data,u32 capacity) {
 char path[24]={};if(!data || capacity!=2396u || !morph_preset_path(path,slot,copy))return 0;
 void* f=stock<void*(const char*,const char*)>(0x8fed0348)(path,"rb");if(!f)return 0;
 const u32 count=stock<u32(void*,u32,u32,void*)>(0x8fed05bc)(data,1,capacity,f);
 opxy::u8 extra=0;const u32 tail=stock<u32(void*,u32,u32,void*)>(0x8fed05bc)(&extra,1,1,f);
 const int closed=stock<int(void*)>(0x8fed00ae)(f);
 return closed || !morph_live_context()?0:tail?capacity+1:count;
}
extern "C" bool sdk_morph_preset_write(u32 slot,u32 copy,const opxy::u8* data,u32 bytes) {
 char path[24]={};if(!data || bytes!=2396u || !morph_preset_path(path,slot,copy))return false;
 void* f=stock<void*(const char*,const char*)>(0x8fed0348)(path,"wb");if(!f)return false;
 const u32 count=stock<u32(const void*,u32,u32,void*)>(0x8fed0780)(data,1,bytes,f);
 const int closed=stock<int(void*)>(0x8fed00ae)(f);
 return count==bytes && !closed && morph_live_context();
}
