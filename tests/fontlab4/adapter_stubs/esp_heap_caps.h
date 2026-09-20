#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline bool fontlab4_test_fail_allocation=false;
inline void* heap_caps_malloc(size_t n,int){return fontlab4_test_fail_allocation?nullptr:std::malloc(n);}
inline void heap_caps_free(void* p){std::free(p);}
