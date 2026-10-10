#pragma once
// Only immutable raw bytes belong to this cache. Fresh consumer copies preserve game resource ownership.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace sm64ds_native_fs {
struct CacheStats {std::uint64_t hits=0,misses=0,inserts=0,evictions=0,oversize=0,allocation_failures=0,bytes_copied=0,pressure_recoveries=0;};
class PristineRawCache {
public:
 static constexpr std::size_t budget=256*1024,max_asset=64*1024,slots=32,path_limit=520;
 using Alloc=void*(*)(std::size_t);using Free=void(*)(void*);
 explicit PristineRawCache(Alloc a=std::malloc,Free f=std::free):allocate(a),deallocate(f){}
 ~PristineRawCache(){clear();}
 PristineRawCache(const PristineRawCache&)=delete;
 PristineRawCache& operator=(const PristineRawCache&)=delete;
 const unsigned char* find(unsigned id,const char* path,std::size_t& size){
  if(!valid(path)){++stats.misses;return nullptr;}
  for(auto&e:entries)if(e.data && e.id==id && !std::strcmp(e.path,path)){
   e.age=++clock;size=e.size;++stats.hits;return e.data;
  }
  ++stats.misses;return nullptr;
 }
 bool insert(unsigned id,const char* path,const void* raw,std::size_t size){
  if(!valid(path)||!raw||!size||size>max_asset||size>budget){++stats.oversize;return false;}
  for(auto&e:entries)if(e.data && e.id==id && !std::strcmp(e.path,path))return true;
  Entry* target=nullptr;
  while(used+size>budget || !(target=vacant()))evict_oldest();
  // Eviction before allocation keeps physical cache payload <= budget even transiently.
  auto* copy=static_cast<unsigned char*>(allocate(size));
  if(!copy){++stats.allocation_failures;return false;}
  std::memcpy(copy,raw,size);target->id=id;std::strcpy(target->path,path);
  target->data=copy;target->size=size;target->age=++clock;
  used+=size;if(used>peak)peak=used;++stats.inserts;return true;
 }
 void copied(std::size_t size){stats.bytes_copied+=size;}
 bool release_for_pressure(){if(!used)return false;clear();++stats.pressure_recoveries;return true;}
 unsigned char* copy_hit(unsigned id,const char* path,std::size_t& size,Alloc raw_allocate=std::malloc){
  const auto* raw=find(id,path,size);if(!raw)return nullptr;
  auto* fresh=static_cast<unsigned char*>(raw_allocate(size));
  // The cached source becomes invalid after pressure release. Return to original
  // file loading, which now has all cache payload memory available again.
  if(!fresh){release_for_pressure();return nullptr;}
  std::memcpy(fresh,raw,size);copied(size);return fresh;
 }
 unsigned char* allocate_original_raw(std::size_t size,Alloc raw_allocate=std::malloc){
  auto* fresh=static_cast<unsigned char*>(raw_allocate(size));
  if(!fresh && release_for_pressure())fresh=static_cast<unsigned char*>(raw_allocate(size));
  return fresh;
 }
 void clear(){for(auto&e:entries)if(e.data){deallocate(e.data);e=Entry{};}used=0;}
 std::size_t payload_bytes()const{return used;}
 std::size_t peak_payload_bytes()const{return peak;}
 const CacheStats& counters()const{return stats;}
private:
 struct Entry {unsigned id=0;char path[path_limit]{};unsigned char* data=nullptr;std::size_t size=0;std::uint64_t age=0;};
 Entry entries[slots];Alloc allocate;Free deallocate;std::size_t used=0,peak=0;std::uint64_t clock=0;CacheStats stats;
 static bool valid(const char* path){return path && std::strlen(path)<path_limit;}
 Entry* vacant(){for(auto&e:entries)if(!e.data)return &e;return nullptr;}
 void evict_oldest(){Entry* oldest=nullptr;for(auto&e:entries)if(e.data && (!oldest||e.age<oldest->age))oldest=&e;
  if(oldest){used-=oldest->size;deallocate(oldest->data);*oldest=Entry{};++stats.evictions;}
 }
};
}
