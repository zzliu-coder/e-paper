#include "../../components/paper_core/vendor/tjpgd/tjpgd.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstring>
#include <cstdlib>
struct Stream{std::vector<unsigned char> input,pixels;size_t pos=0;unsigned width=0;};
size_t input(JDEC*d,uint8_t*out,size_t n){auto&s=*static_cast<Stream*>(d->device);n=std::min(n,s.input.size()-s.pos);if(out)memcpy(out,s.input.data()+s.pos,n);s.pos+=n;return n;}
int output(JDEC*d,void*b,JRECT*r){auto&s=*static_cast<Stream*>(d->device);size_t n=r->right-r->left+1;for(unsigned y=r->top;y<=r->bottom;++y)memcpy(s.pixels.data()+y*s.width+r->left,static_cast<uint8_t*>(b)+(y-r->top)*n,n);return 1;}
int main(int argc,char**argv){assert(argc==4);Stream s;std::ifstream f(argv[1],std::ios::binary);assert(f);s.input.assign(std::istreambuf_iterator<char>(f),{});std::vector<unsigned char>pool(16384);JDEC d{};assert(jd_prepare(&d,input,pool.data(),pool.size(),&s)==JDR_OK);unsigned scale=std::atoi(argv[3]);assert(scale<=3);s.width=d.width>>scale;unsigned h=d.height>>scale;s.pixels.assign(s.width*h,37);assert(jd_decomp(&d,output,scale)==JDR_OK);std::ofstream o(argv[2],std::ios::binary);o<<"P5\n"<<s.width<<' '<<h<<"\n255\n";o.write(reinterpret_cast<char*>(s.pixels.data()),s.pixels.size());}
