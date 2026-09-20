#include "ui/fontlab4/core.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
using namespace paper::lab4;
int main(int argc,char** argv){
    assert(argc==3);std::string dir=argv[1],scratch=argv[2];std::string error;
    assert(Crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
    auto page=ReadPage(dir,0,error);assert(page&&error.empty());unsigned total=page->count;
    unsigned samples=0,refreshTarget=0;size_t maxStatus=0;Session statusSession(dir);
    for(unsigned id=0;id<total;++id){
        auto p=ReadPage(dir,id,error);assert(p&&p->id==id&&p->pixels.size()==48000);
        for(const auto& s:p->samples){assert(s.valid&&s.px==s.resolved);++samples;}
        if(p->refresh&&!p->alternate)refreshTarget=id;
        assert(statusSession.Load(id));statusSession.Painted(true,id+1);
        auto* status=statusSession.Status();char* encoded=cJSON_PrintUnformatted(status);
        assert(encoded);maxStatus=std::max(maxStatus,strlen(encoded));assert(strlen(encoded)<1800);
        cJSON_free(encoded);cJSON_Delete(status);
    }
    Session s(dir);assert(s.Enter());assert(!s.canVote());s.Painted(true,1);assert(s.canVote());
    unsigned last=unsigned(s.page()->samples.size()-1);auto box=s.page()->samples[last].box;
    assert(s.Tap(box.x+1,box.y+1)==Action::Redraw);s.Painted(true,2);assert(s.selectedIndex()==last);
    auto json=s.VoteJson(4,"native-test-not-a-device",123);assert(json.find(s.selected()->id)!=std::string::npos);
    assert(s.VoteJson(-1,"test",0).empty());assert(s.VoteJson(5,"test",0).empty());
    assert(s.Tap(418,758)==Action::Vote);
    assert(s.SaveVote(scratch+"/ratings.jsonl","native-test-not-a-device",123));
    assert(!s.SaveVote(scratch+"/ratings.jsonl","native-test-not-a-device",123)); // pending vote consumed
    s.Load(refreshTarget);s.Painted(true,10);auto target=s.page()->pixels;
    assert(s.StartSequence(4));assert(!s.canVote());
    for(int n=0;n<8;++n){
        s.Painted(n==6,20+n); // Deliberate scheduler-promoted full refresh is recorded, not hidden.
        if(n!=7){assert(s.running());assert(s.AdvanceSequence());}
    }
    assert(!s.running()&&s.canVote()&&s.page()->id==refreshTarget&&s.page()->pixels==target);
    json=s.VoteJson(0,"native-test-not-a-device",200);
    assert(json.find("\"full\":true")!=std::string::npos);assert(s.fastCount()==1);
    assert(!s.StartSequence(5));
    auto* status=s.Status();char* text=cJSON_PrintUnformatted(status);assert(text&&strlen(text)<1800);cJSON_free(text);cJSON_Delete(status);
    assert(!s.Load(4096));assert(!s.page()&&!s.canVote());assert(s.VoteJson(0,"test",0).empty());
    Session missing(scratch+"/missing");assert(!missing.Enter()&&!missing.canVote());
    for(const auto& entry:std::filesystem::directory_iterator(scratch+"/invalid")){
        if(!entry.is_directory())continue;
        assert(!ReadPage(entry.path().string(),0,error));
    }
    // Invalid/mis-sized samples can be inspected but are barred from visual scoring.
    Session invalid(scratch+"/unavailable");assert(invalid.Enter());invalid.Painted(true,1);assert(!invalid.canVote());
    auto goodId=s.page()?s.page()->id:0;(void)goodId;
    std::printf("{\"result\":\"PASS\",\"pages\":%u,\"samples\":%u,\"refresh_sequence_frames\":8,\"max_lab_status_bytes\":%zu}\n",total,samples,maxStatus);
}
