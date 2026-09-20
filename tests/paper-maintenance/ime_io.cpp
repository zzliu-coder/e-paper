#include "paper/ime.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace paper;
static uint32_t u32(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static uint16_t u16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;}
// Independent old seek-per-row/payload oracle. Production fixture only.
static std::vector<Candidate> reference(const std::string&path,const std::string&q){
    FILE*f=fopen(path.c_str(),"rb");assert(f);uint8_t h[32],r[64];
    assert(fread(h,1,32,f)==32);auto n=u32(h+8),data=u32(h+16);
    auto row=[&](uint32_t i){assert(!fseek(f,32+long(i)*64,SEEK_SET));assert(fread(r,1,64,f)==64);};
    uint32_t lo=0,hi=n;while(lo<hi){auto m=lo+(hi-lo)/2;row(m);if(std::string((char*)r)<q)lo=m+1;else hi=m;}
    std::vector<Candidate> out;
    for(uint32_t i=lo;i<n&&i-lo<2048;++i){
        row(i);std::string key((char*)r);if(key.compare(0,q.size(),q))break;
        auto off=u32(r+48);auto wl=u16(r+52),pl=u16(r+54);auto freq=u32(r+56);
        char b[240];assert(wl+pl<=240);assert(!fseek(f,data+off,SEEK_SET));assert(fread(b,1,wl+pl,f)==size_t(wl+pl));
        Candidate c{std::string(b,wl),std::string(b+wl,pl),std::min<uint32_t>(freq,1000000)+(key==q?2000000u:0u)};
        auto same=std::find_if(out.begin(),out.end(),[&](auto&x){return x.text==c.text;});
        if(same==out.end())out.push_back(c);else if(c.score>same->score)*same=c;
        std::stable_sort(out.begin(),out.end(),[](auto&a,auto&b){if(a.score!=b.score)return a.score>b.score;if(a.text.size()!=b.text.size())return a.text.size()<b.text.size();return a.text<b.text;});
        if(out.size()>64)out.resize(64);
    }
    fclose(f);return out;
}
int main(int argc,char**argv){
    assert(argc==2);ResourceGate gate;Store store(argv[1],gate);assert(store.initialize());PinyinDictionary dict(store);
    unsigned cases=0;
    for(bool nine:{false,true})for(auto q:{"a","n","ni","nihao","y","yu","yue","yuedu","zhong","zhonghua","z","zzzz"}){
        std::string key=nine?PinyinDictionary::nineKey(q):q;std::vector<Candidate> actual;
        assert(dict.query(key,nine,actual));
        auto expected=reference(std::string(argv[1])+"/paper/ime/"+(nine?"nine":"pinyin")+".pim",key);
        assert(actual.size()==expected.size());
        for(size_t i=0;i<actual.size();++i){assert(actual[i].text==expected[i].text);assert(actual[i].pinyin==expected[i].pinyin);assert(actual[i].score==expected[i].score);}
        ++cases;
    }
    std::cout<<"PASS "<<cases<<" full-pinyin/nine-key queries equal old per-row oracle\n";
}
