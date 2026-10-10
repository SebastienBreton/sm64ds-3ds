// Port-side buffers must not consume the original game's scene/root heaps.
#ifndef NTR_HOST_ALLOCATOR_H
#define NTR_HOST_ALLOCATOR_H
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>
namespace ntr {
template<class T> struct HostAllocator {
    using value_type=T;
    HostAllocator() noexcept=default;
    template<class U> HostAllocator(const HostAllocator<U>&) noexcept {}
    template<class U> struct rebind {using other=HostAllocator<U>;};
    T* allocate(std::size_t count) {
        static_assert(alignof(T)<=alignof(std::max_align_t),"over-aligned host allocation requires a separate allocator");
        if(count>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::bad_alloc();
        auto* memory=std::malloc(count*sizeof(T));
        if(!memory && count)throw std::bad_alloc();
        return static_cast<T*>(memory);
    }
    void deallocate(T* memory,std::size_t) noexcept {std::free(memory);}
};
template<class T,class U> bool operator==(const HostAllocator<T>&,const HostAllocator<U>&){return true;}
template<class T,class U> bool operator!=(const HostAllocator<T>&,const HostAllocator<U>&){return false;}
template<class T> using HostVector=std::vector<T,HostAllocator<T>>;
}
#endif
