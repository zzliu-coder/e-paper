// Historical experiment test, retained for evidence; requires grayab6 runtime.
#include "paper/runtime.hpp"
#include <cassert>
#include <iostream>
struct Hardware:paper::Hardware{
    paper::HostHardware host;bool enabled=true;
    bool supports(const std::string&k)const override{return k=="gray4_experiment"?enabled:host.supports(k);}
    paper::Status get(const std::string&k,std::string&v)override{return host.get(k,v);}
    paper::Status set(const std::string&k,const std::string&v)override{return host.set(k,v);}
    paper::Status action(const std::string&k)override{return host.action(k);}
    std::string environment()const override{return "host-gray-test";}
};
int main(int argc,char**argv){
    assert(argc==2);Hardware hw;paper::Runtime r(argv[1],hw);assert(r.initialize());
    paper::DisplayJob b;
    assert(!r.action("gray-test","stroke"));
    assert(r.action("gray-test","gray4"));assert(r.job(b));assert(b.gray);
    for(int i=0;i<4;++i)assert(b.frame.pixel(50+i*108,300)==i);
    assert(r.complete(b.revision,{}));assert(r.action("gray-test-next"));assert(r.job(b));assert(r.complete(b.revision,{}));
    for(int page=0;page<5;++page){
        paper::Canvas baseline;
        for(auto mode:{"mono","dots","gray4"}){
            bool gray=std::string(mode)=="gray4";
            assert(r.action("gray-test",mode));assert(r.job(b));assert(b.gray==gray);
            unsigned intermediate=0;
            for(int y=0;y<800;++y)for(int x=0;x<480;++x){auto p=b.frame.pixel(x,y);if(p==1||p==2)++intermediate;}
            assert(gray?intermediate>0:intermediate==0);
            if(page<4){
                for(int y=0;y<44;++y)for(int x=32;x<448;++x)
                    assert(b.frame.pixel(x,266+y)+b.frame.pixel(x,442+y)==3);
                if(std::string(mode)=="mono")baseline=b.frame;
                else {size_t changed=0;for(int y=266;y<310;++y)for(int x=32;x<448;++x)changed+=baseline.pixel(x,y)!=b.frame.pixel(x,y);assert(changed>0);}
            }else if(gray){for(int i=0;i<4;++i)assert(b.frame.pixel(50+i*108,300)==i);}
            else if(std::string(mode)=="dots"){
                int white=0;for(int y=254;y<406;++y)for(int x=136;x<224;++x)white+=b.frame.pixel(x,y)==3;
                assert(white==152*88/4);
            }
            auto crc=b.frame.checksum();assert(r.complete(b.revision,{}));
            assert(r.action("refresh"));assert(r.job(b));assert(b.full&&b.gray==gray&&b.frame.checksum()==crc);assert(r.complete(b.revision,{}));
        }
        assert(r.action("gray-test-next"));assert(r.job(b));assert(r.complete(b.revision,{}));
    }
    assert(r.action("home"));assert(r.job(b));assert(!b.gray);
    assert(r.snapshot().find("\"reader_gray\":\"mono\"")!=std::string::npos);
    assert(!r.action("gray-test","invalid"));hw.enabled=false;assert(!r.action("gray-test","gray4"));
    std::cout<<"PASS: 15 regular400 frames, first-entry calibration, gray levels, inverse symmetry, binary filters, full refresh, capability gate, defaults unchanged; physical quality NOT_PROVEN\n";
}
