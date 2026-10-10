#pragma once
#include "platform/3ds/native_geometry_clear.h"
#include <memory>
namespace held_raster {
struct Parameters {
 ds_clear::State clear;
 float slider=0,convergence=0,aspect=1;
 bool perspective=false,shadows=false;
 bool operator==(const Parameters& b)const{return clear==b.clear && slider==b.slider && convergence==b.convergence && aspect==b.aspect && perspective==b.perspective && shadows==b.shadows;}
};
// Pixel storage is the existing resident pair, never a new framebuffer. The
// caller invalidates before any other writer and after resident destruction.
// No owner is kept alive just for a cache hit; expired/recycled frames miss.
class Lease {
 std::weak_ptr<const void> owner_;Parameters parameters_;
public:
 bool matches(const std::shared_ptr<const void>& owner,const Parameters& parameters)const{
  auto previous=owner_.lock();return owner && previous && previous==owner && parameters_==parameters;
 }
 void invalidate(){owner_.reset();}
 void commit(const std::shared_ptr<const void>& owner,const Parameters& parameters){owner_=owner;parameters_=parameters;}
};
}
