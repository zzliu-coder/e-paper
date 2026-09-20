#include "core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/stat.h>

namespace paper::lab4 {
namespace {
uint32_t le32(const uint8_t* b){return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
const cJSON* field(const cJSON* o,const char* key){return cJSON_GetObjectItemCaseSensitive(o,key);}
bool number(const cJSON* v,double low,double high,unsigned& result){
    if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||v->valuedouble<low||v->valuedouble>high||std::floor(v->valuedouble)!=v->valuedouble)return false;
    result=static_cast<unsigned>(v->valuedouble);return true;
}
bool num(const cJSON* o,const char* key,double low,double high,unsigned& result){return number(field(o,key),low,high,result);}
bool text(const cJSON* v,std::string& out,size_t limit){
    if(!cJSON_IsString(v)||!v->valuestring)return false;
    const size_t n=std::strlen(v->valuestring);if(n==0||n>limit)return false;
    out.assign(v->valuestring,n);return true;
}
bool hex(const std::string& s,size_t n){return s.size()==n&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool identifier(const std::string& s){
    return std::all_of(s.begin(),s.end(),[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';});
}
bool flag(const cJSON* o,const char* key,bool& out){const auto* v=field(o,key);if(!cJSON_IsBool(v))return false;out=cJSON_IsTrue(v);return true;}
void add(cJSON* o,const char* key,unsigned n){cJSON_AddNumberToObject(o,key,n);}
}
uint32_t Crc32(const uint8_t* b,size_t n,uint32_t initial){
    uint32_t c=initial^0xffffffffu;
    for(size_t i=0;i<n;++i){c^=b[i];for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}
    return c^0xffffffffu;
}
uint32_t RegionCrc(const std::vector<uint8_t>& frame,Rect b){
    if(frame.size()!=FrameBytes||b.x<0||b.y<0||b.w<=0||b.h<=0||b.x>Width-b.w||b.y>Height-b.h)return 0;
    uint32_t crc=0;
    // One byte per physical pixel, matching Python region_bytes().
    for(int y=b.y;y<b.y+b.h;++y){
        uint8_t row[Width];
        for(int x=0;x<b.w;++x){int px=b.x+x;row[x]=(frame[y*(Width/8)+px/8]&(128>>(px&7)))?255:0;}
        crc=Crc32(row,size_t(b.w),crc);
    }
    return crc;
}
std::unique_ptr<Page> ReadPage(const std::string& dir,unsigned id,std::string& error){
    error.clear();
    if(id>=MaxPages){error="BAD PAGE ID";return nullptr;}
    char name[24];std::snprintf(name,sizeof(name),"/%04u.fl4",id);
    FILE* f=std::fopen((dir+name).c_str(),"rb");
    if(!f){error="SD PAGE UNAVAILABLE";return nullptr;}
    struct Close{FILE* f;~Close(){std::fclose(f);}} close{f};
    uint8_t header[32];
    if(std::fread(header,1,32,f)!=32||std::memcmp(header,"FLAB4P\0\0",8)||le32(header+8)!=4||le32(header+28)!=0){error="BAD PAGE HEADER";return nullptr;}
    const uint32_t mlen=le32(header+12),flen=le32(header+16);
    if(mlen==0||mlen>16384||flen!=FrameBytes){error="PAGE SIZE LIMIT";return nullptr;}
    auto page=std::make_unique<Page>();std::vector<char> raw(mlen+1);page->pixels.resize(FrameBytes);
    if(std::fread(raw.data(),1,mlen,f)!=mlen||std::fread(page->pixels.data(),1,flen,f)!=flen||std::fgetc(f)!=EOF||std::ferror(f)){
        error="TRUNCATED OR TRAILING PAGE";return nullptr;}
    if(std::memchr(raw.data(),0,mlen)||Crc32(reinterpret_cast<const uint8_t*>(raw.data()),mlen)!=le32(header+20)||Crc32(page->pixels.data(),flen)!=le32(header+24)){
        error="PAGE CRC MISMATCH";return nullptr;}
    const char* end=nullptr;
    page->json=cJSON_ParseWithLengthOpts(raw.data(),mlen+1,&end,1);
    if(!page->json||!cJSON_IsObject(page->json)){error="BAD PAGE JSON";return nullptr;}
    const auto* root=page->json;unsigned value=0;
    if(!num(root,"schema",4,4,value)||!num(root,"width",480,480,value)||!num(root,"height",800,800,value)||!num(root,"output_bpp",1,1,value)||
       !num(root,"page_id",id,id,page->id)||!num(root,"page_count",1,MaxPages,page->count)||page->id>=page->count||
       !num(root,"group",0,5,page->group)||!text(field(root,"build_id"),page->buildId,32)||!hex(page->buildId,32)||
       !text(field(root,"title"),page->title,128)||!flag(root,"refresh",page->refresh)||!flag(root,"alternate",page->alternate)){
        error="PAGE IDENTITY MISMATCH";return nullptr;}
    const auto* nav=field(root,"nav");const auto* groups=field(nav,"groups");
    if(!num(nav,"prev",0,page->count-1,page->previous)||!num(nav,"next",0,page->count-1,page->next)||!cJSON_IsArray(groups)||cJSON_GetArraySize(groups)!=6){error="BAD NAVIGATION";return nullptr;}
    for(int i=0;i<6;++i)if(!number(cJSON_GetArrayItem(groups,i),0,page->count-1,page->groups[i])){error="BAD GROUP ID";return nullptr;}
    if(page->refresh&&(!num(root,"alternate_id",0,page->count-1,page->alternateId)||page->alternateId==id)){error="BAD REFRESH PAIR";return nullptr;}
    if(page->alternate&&!page->refresh){error="INVALID ALTERNATE";return nullptr;}
    const auto* samples=field(root,"samples");
    if(!cJSON_IsArray(samples)||cJSON_GetArraySize(samples)<1||cJSON_GetArraySize(samples)>5){error="SAMPLE LIMIT";return nullptr;}
    for(int i=0;i<cJSON_GetArraySize(samples);++i){
        const auto* item=cJSON_GetArrayItem(samples,i);Sample sample;sample.json=item;
        unsigned requested=0,resolved=0,threshold=0,bpp=0,regionCrc=0;bool declared=false,fallback=true;std::string polarity;
        const auto* missing=field(item,"missing_glyphs");
        if(!text(field(item,"sample_id"),sample.id,96)||!text(field(item,"font_id"),sample.font,16)||
           !text(field(item,"algorithm"),sample.algorithm,4)||!text(field(item,"region_sha256"),sample.sha256,64)||!hex(sample.sha256,64)||
           !num(item,"requested_px",16,40,requested)||!num(item,"resolved_px",16,40,resolved)||!num(item,"threshold",1,254,threshold)||
           !num(item,"input_bpp",1,8,bpp)||!(bpp==1||bpp==2||bpp==4||bpp==8)||!num(item,"output_bpp",1,1,value)||
           !flag(item,"resolved_ok",declared)||!flag(item,"fallback_used",fallback)||!cJSON_IsArray(missing)||
           !text(field(item,"polarity"),polarity,8)||(polarity!="normal"&&polarity!="inverse")||
           !num(item,"region_crc32",0,4294967295.0,regionCrc)){
            error="BAD SAMPLE IDENTITY";return nullptr;}
        sample.crc=static_cast<uint32_t>(regionCrc);
        if(!identifier(sample.id)||!identifier(sample.font)){error="BAD SAMPLE IDENTIFIER";return nullptr;}
        if(sample.algorithm!="M-A"&&sample.algorithm!="M-N"&&sample.algorithm!="N-A"&&sample.algorithm!="L-A"&&sample.algorithm!="N-N"){
            error="UNKNOWN ALGORITHM";return nullptr;}
        const auto* r=field(item,"box");unsigned coords[4]{};
        if(!cJSON_IsArray(r)||cJSON_GetArraySize(r)!=4){error="BAD SAMPLE RECT";return nullptr;}
        for(int k=0;k<4;++k)if(!number(cJSON_GetArrayItem(r,k),0,800,coords[k])){error="BAD SAMPLE RECT";return nullptr;}
        sample.box={int(coords[0]),int(coords[1]),int(coords[2]),int(coords[3])};
        const auto& b=sample.box;
        if(b.x<20||b.y<200||b.w<=0||b.h<=0||b.x>460-b.w||b.y>690-b.h||RegionCrc(page->pixels,b)!=sample.crc){error="SAMPLE PIXEL MISMATCH";return nullptr;}
        for(const auto& earlier:page->samples){
            const auto& e=earlier.box;
            if(earlier.id==sample.id||(b.x<e.x+e.w&&b.x+b.w>e.x&&b.y<e.y+e.h&&b.y+b.h>e.y)){error="AMBIGUOUS SAMPLE";return nullptr;}
        }
        sample.px=int(requested);sample.resolved=int(resolved);sample.threshold=int(threshold);sample.inputBpp=int(bpp);sample.inverse=polarity=="inverse";
        sample.valid=declared&&!fallback&&cJSON_GetArraySize(missing)==0&&requested==resolved;
        page->samples.push_back(std::move(sample));
    }
    return page;
}

bool Session::Enter(){bundle_.clear();remaining_=0;trace_.clear();return Load(0);}
bool Session::Load(unsigned id,bool full){
    if(full){remaining_=0;trace_.clear();}
    auto next=ReadPage(directory_,id,error_);painted_=false;pendingVote_=-1;selected_=0;fullRequested_=full;feedbackText_="SELECT SAMPLE";
    if(next&&!bundle_.empty()&&next->buildId!=bundle_){next.reset();error_="MIXED RESOURCE BUNDLE";}
    if(next){bundle_=next->buildId;page_=std::move(next);return true;}
    page_.reset();remaining_=0;feedbackText_=error_;return false;
}
const Sample* Session::selected()const{return page_&&selected_<page_->samples.size()?&page_->samples[selected_]:nullptr;}
bool Session::canVote()const{return painted_&&!running()&&page_&&!page_->alternate&&selected()&&selected()->valid;}
Action Session::Step(int d){if(running())return Action::None;Load(page_?(d<0?page_->previous:page_->next):0);return Action::Redraw;}
Action Session::Tap(int x,int y){
    if(running())return Action::None;
    if(Rect{12,8,88,48}.contains(x,y))return Action::Exit;
    if(Rect{392,8,80,48}.contains(x,y))return Action::Legacy;
    if(y>=66&&y<114){for(int i=0;i<6;++i)if(Rect{20+i*74,66,70,48}.contains(x,y)){Load(page_?page_->groups[i]:0);return Action::Redraw;}}
    if(Rect{20,122,100,48}.contains(x,y))return Step(-1);
    if(Rect{360,122,100,48}.contains(x,y))return Step(1);
    if(Rect{184,122,112,48}.contains(x,y)){remaining_=0;trace_.clear();Load(page_?page_->id:0);return Action::Redraw;}
    if(page_){
        for(unsigned i=0;i<page_->samples.size();++i)if(page_->samples[i].box.contains(x,y)){
            selected_=i;feedbackText_=page_->samples[i].valid?"SELECTED":"SAMPLE UNAVAILABLE";
            fullRequested_=!page_->refresh;return Action::Redraw;}
        if(page_->refresh&&y>=650&&y<695){
            if(Rect{20,650,210,45}.contains(x,y)&&StartSequence(1))return Action::Redraw;
            if(Rect{250,650,210,45}.contains(x,y)&&StartSequence(4))return Action::Redraw;
        }
        for(int i=0;i<5;++i)if(Rect{20+i*88,736,84,48}.contains(x,y)){
            if(!canVote()){feedbackText_="VOTE BLOCKED: INVALID SAMPLE";fullRequested_=true;return Action::Redraw;}
            pendingVote_=i;return Action::Vote;
        }
    }
    return Action::None;
}
void Session::Painted(bool full,uint32_t revision){
    painted_=page_!=nullptr;lastFull_=full;revision_=revision;fastCount_=full?0:fastCount_+1;
    if(remaining_){trace_.push_back({page_->id,revision,full});--remaining_;++step_;}
}
void Session::PaintFailed(){painted_=false;remaining_=0;error_="DISPLAY FAILED";feedbackText_=error_;}
bool Session::StartSequence(unsigned pairs){
    if(!painted_||!page_||!page_->refresh||page_->alternate||running()||pairs<1||pairs>4)return false;
    sequenceTarget_=page_->id;sequenceOther_=page_->alternateId;step_=0;trace_.clear();remaining_=pairs*2;
    if(!Load(sequenceOther_,false))return false;
    if(!page_->alternate||!page_->refresh||page_->alternateId!=sequenceTarget_){PaintFailed();error_="PAIR IDENTITY MISMATCH";return false;}
    return true;
}
bool Session::AdvanceSequence(){
    if(!remaining_)return false;
    const bool alternate=(step_%2)==0;const unsigned target=alternate?sequenceOther_:sequenceTarget_;
    if(!Load(target,false))return false;
    if(page_->alternate!=alternate||!page_->refresh||page_->alternateId!=(alternate?sequenceTarget_:sequenceOther_)){
        PaintFailed();error_="PAIR IDENTITY MISMATCH";return false;}
    return true;
}
std::string Session::VoteJson(int feedback,const char* boot,uint32_t uptime)const{
    if(!canVote()||feedback<0||feedback>4||!boot)return {};
    cJSON* root=cJSON_Duplicate(selected()->json,1);if(!root)return {};
    add(root,"schema",4);cJSON_AddStringToObject(root,"build_id",page_->buildId.c_str());
    cJSON_AddStringToObject(root,"firmware","1.0.0-fontlab4");cJSON_AddStringToObject(root,"boot_id",boot);
    add(root,"uptime_ms",uptime);add(root,"page_id",page_->id);add(root,"group",page_->group);
    add(root,"frame_revision",revision_);add(root,"fast_refresh_count",fastCount_);
    cJSON_AddBoolToObject(root,"last_full",lastFull_);add(root,"feedback",unsigned(feedback));
    cJSON_AddStringToObject(root,"evidence","human_vote; pixels_prevalidated; optical_quality_not_automated");
    auto* trace=cJSON_AddArrayToObject(root,"sequence");
    for(const auto& e:trace_){auto* item=cJSON_CreateObject();add(item,"page",e.page);add(item,"revision",e.revision);cJSON_AddBoolToObject(item,"full",e.full);cJSON_AddItemToArray(trace,item);}
    char* bytes=cJSON_PrintUnformatted(root);std::string result=bytes?bytes:"";cJSON_free(bytes);cJSON_Delete(root);return result;
}
bool Session::SaveVote(const std::string& path,const char* boot,uint32_t uptime){
    const std::string line=VoteJson(pendingVote_,boot,uptime);pendingVote_=-1;
    if(line.empty()||line.size()>8192){feedbackText_="VOTE BLOCKED";return false;}
    struct stat st{};
    if(::stat(path.c_str(),&st)==0&&(st.st_size<0||uint64_t(st.st_size)+line.size()+1>262144)){feedbackText_="LOG FULL";return false;}
    FILE* f=std::fopen(path.c_str(),"ab");bool ok=false;
    if(f){ok=std::fwrite(line.data(),1,line.size(),f)==line.size();ok=(std::fputc('\n',f)!=EOF)&&ok;ok=(std::fflush(f)==0)&&ok;ok=(std::fclose(f)==0)&&ok;}
    feedbackText_=ok?"SAVED TO SD":"SAVE FAILED";fullRequested_=false;return ok;
}
cJSON* Session::Status()const{
    auto* o=cJSON_CreateObject();add(o,"schema",4);cJSON_AddBoolToObject(o,"ready",page_!=nullptr);
    cJSON_AddStringToObject(o,"error",error_.c_str());cJSON_AddStringToObject(o,"feedback",feedbackText_.c_str());
    cJSON_AddBoolToObject(o,"running",running());cJSON_AddBoolToObject(o,"can_vote",canVote());
    cJSON_AddBoolToObject(o,"last_full",lastFull_);add(o,"fast_count",fastCount_);add(o,"pixel_revision",revision_);
    cJSON_AddStringToObject(o,"gray_capability","I1_ONLY; MATCHED_PANEL_WAVEFORM_REQUIRED");
    if(page_){
        add(o,"page_id",page_->id);add(o,"page_count",page_->count);add(o,"group",page_->group);
        cJSON_AddStringToObject(o,"build_id",page_->buildId.c_str());add(o,"selected",selected_);
        cJSON_AddBoolToObject(o,"alternate",page_->alternate);
        auto brief=[](const Sample& s){
            auto* q=cJSON_CreateObject();
            // Keep inkdesk.status well below the existing 4096-byte ML1 TX limit.
            // Full provenance is in the per-page manifest and explicit vote record.
            const char* keys[]={"sample_id","requested_px","resolved_px","box","region_sha256"};
            for(const char* k:keys){const auto* v=field(s.json,k);if(v)cJSON_AddItemToObject(q,k,cJSON_Duplicate(v,1));}
            cJSON_AddBoolToObject(q,"valid",s.valid);return q;
        };
        auto* samples=cJSON_AddArrayToObject(o,"samples");
        for(const auto& s:page_->samples)cJSON_AddItemToArray(samples,brief(s));
    }
    return o;
}
} // namespace paper::lab4
