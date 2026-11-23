// C++ new accounting is intentionally separate from Lua's malloc quota.
#include <cstdlib>
#include <new>
#include <cstddef>
static size_t nativeLiveBytes=0;
struct alignas(std::max_align_t) NativeAllocationHeader{size_t size;};
void* operator new(size_t n){auto* p=static_cast<NativeAllocationHeader*>(std::malloc(sizeof(NativeAllocationHeader)+n));if(!p)throw std::bad_alloc();p->size=n;nativeLiveBytes+=n;return p+1;}
void operator delete(void* value) noexcept {if(!value)return;auto* p=static_cast<NativeAllocationHeader*>(value)-1;nativeLiveBytes-=p->size;std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
