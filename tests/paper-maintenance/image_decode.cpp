#include "paper/image.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <cstring>
static int failAllocation=0;
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    if(failAllocation&&!--failAllocation)return nullptr;
    try{return ::operator new(size);}catch(...){return nullptr;}
}
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept {
    if(failAllocation&&!--failAllocation)return nullptr;
    try{return ::operator new[](size);}catch(...){return nullptr;}
}
int main(int argc,char**argv){
    assert(argc>1);
    for(int i=1;i<argc;++i){
        std::ifstream f(argv[i],std::ios::binary);
        assert(f);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        paper::Canvas canvas;
        auto result=paper::paintBookImage(bytes,canvas,{24,112,432,560});
        if(std::strstr(argv[i],"reject-")){
            assert(!result);assert(canvas.checksum()==paper::Canvas().checksum());
            std::cout<<"PASS rejected: "<<argv[i]<<'\n';continue;
        }
        if(!result){std::cerr<<argv[i]<<": "<<result.message<<'\n';return 1;}
        assert(canvas.checksum()!=paper::Canvas().checksum());
        if(i==1){
            for(int nth:{1,2}){
                failAllocation=nth;paper::Canvas untouched;
                auto oom=paper::paintBookImage(bytes,untouched,{24,112,432,560});
                assert(!oom&&oom.code==paper::Error::Unavailable);
                assert(untouched.checksum()==paper::Canvas().checksum());
                failAllocation=0;
            }
        }
        bytes.resize(20);
        assert(!paper::paintBookImage(bytes,canvas,{24,112,432,560}));
        std::cout<<"PASS image: "<<argv[i]<<'\n';
    }
}
