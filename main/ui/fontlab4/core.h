#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "cJSON.h"

namespace paper::lab4 {
constexpr int Width=480, Height=800, FrameBytes=48000, MaxPages=4096;
struct Rect {
    int x=0,y=0,w=0,h=0;
    bool contains(int a,int b)const{return a>=x&&b>=y&&a<x+w&&b<y+h;}
};
struct Sample {
    std::string id,font,algorithm,sha256;
    int px=0,resolved=0,threshold=0,inputBpp=0;
    bool inverse=false,valid=false;
    Rect box;
    uint32_t crc=0;
    const cJSON* json=nullptr; // Owned by Page::json.
};
struct Page {
    unsigned id=0,count=0,group=0,previous=0,next=0,alternateId=0;
    std::array<unsigned,6> groups{};
    std::string buildId,title;
    bool refresh=false,alternate=false;
    std::vector<uint8_t> pixels;
    std::vector<Sample> samples;
    cJSON* json=nullptr;
    ~Page(){cJSON_Delete(json);}
    Page()=default;
    Page(const Page&)=delete;
    Page& operator=(const Page&)=delete;
};
uint32_t Crc32(const uint8_t* bytes,size_t n,uint32_t initial=0);
uint32_t RegionCrc(const std::vector<uint8_t>& frame,Rect box);
std::unique_ptr<Page> ReadPage(const std::string& directory,unsigned id,std::string& error);

enum class Action {None,Redraw,Exit,Legacy,Vote};
struct RefreshEvent {unsigned page=0;uint32_t revision=0;bool full=false;};
class Session {
 public:
    explicit Session(std::string directory="/sdcard/inkdesk/font-lab4") : directory_(std::move(directory)) {}
    bool Enter();
    bool Load(unsigned id,bool full=true);
    Action Tap(int x,int y);
    Action Step(int direction);
    void Painted(bool full,uint32_t revision);
    void PaintFailed();
    bool AdvanceSequence();
    bool StartSequence(unsigned pairs);
    bool SaveVote(const std::string& path,const char* boot,uint32_t uptime);
    std::string VoteJson(int feedback,const char* boot,uint32_t uptime)const;
    cJSON* Status()const;
    const Page* page()const{return page_.get();}
    const Sample* selected()const;
    bool canVote()const;
    bool fullRequested()const{return fullRequested_;}
    bool running()const{return remaining_!=0;}
    uint32_t revision()const{return revision_;}
    const std::string& error()const{return error_;}
    const std::string& feedbackText()const{return feedbackText_;}
    unsigned selectedIndex()const{return selected_;}
    unsigned fastCount()const{return fastCount_;}
 private:
    std::string directory_,bundle_,error_,feedbackText_="SELECT SAMPLE";
    std::unique_ptr<Page> page_;
    unsigned selected_=0,fastCount_=0,remaining_=0,sequenceTarget_=0,sequenceOther_=0,step_=0;
    uint32_t revision_=0;
    bool painted_=false,fullRequested_=true,lastFull_=true;
    int pendingVote_=-1;
    std::vector<RefreshEvent> trace_;
};
} // namespace paper::lab4
