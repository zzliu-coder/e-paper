#include "core.h"
#include "baseline_identity.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
namespace paper::fontbench {
namespace {
using Json=std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
const cJSON* item(const cJSON* j,const char* key){return cJSON_GetObjectItemCaseSensitive(j,key);}
bool integer(const cJSON* j,int lo,int hi,int& out){if(!cJSON_IsNumber(j)||!std::isfinite(j->valuedouble)||j->valuedouble<lo||j->valuedouble>hi||std::floor(j->valuedouble)!=j->valuedouble)return false;out=static_cast<int>(j->valuedouble);return true;}
bool u32(const cJSON* j,uint32_t& out){if(!cJSON_IsNumber(j)||!std::isfinite(j->valuedouble)||j->valuedouble<0||j->valuedouble>4294967295.0||std::floor(j->valuedouble)!=j->valuedouble)return false;out=static_cast<uint32_t>(j->valuedouble);return true;}
std::string str(const cJSON* j){return cJSON_IsString(j)&&j->valuestring?j->valuestring:"";}
bool read(const std::string& path,size_t budget,std::vector<uint8_t>& out){
 FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
 bool ok=std::fseek(f,0,SEEK_END)==0;long n=ok?std::ftell(f):-1;std::rewind(f);
 if(n<0||static_cast<unsigned long>(n)>budget)ok=false;
 if(ok){out.resize(static_cast<size_t>(n));ok=std::fread(out.data(),1,out.size(),f)==out.size()&&!std::ferror(f);}
 return std::fclose(f)==0&&ok;
}
int wi(int w){return w==400?0:w==500?1:w==700?2:-1;}
uint32_t le(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void pixel(std::vector<uint8_t>& p,int x,int y,bool white){if(x<0||x>=Width||y<0||y>=Height)return;const size_t i=(y*Width+x)/8;const uint8_t mask=uint8_t(128u>>(x&7));if(white)p[i]|=mask;else p[i]&=uint8_t(~mask);}
void invert(std::vector<uint8_t>& p,Rect b){for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x)p[(y*Width+x)/8]^=uint8_t(128u>>(x&7));}
}
uint32_t Crc32(const uint8_t* p,size_t n){uint32_t crc=0xffffffffu;for(size_t i=0;i<n;++i){crc^=p[i];for(int k=0;k<8;++k)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}return crc^0xffffffffu;}
bool Valid(int f,int v){switch(f){case 0:return v>=0&&v<FontCount;case 1:return v>=16&&v<=40;case 2:return v>=0&&v<AlgorithmCount;case 3:return v==112||v==128||v==144;case 4:return wi(v)>=0;case 5:return v>=0&&v<4;case 6:return v==0||v==1;case 7:return v==2||v==4||v==8;case 8:return (v>=0&&v<4)||v==10;case 9:return v>=0&&v<8;case 10:return v==0||v==1;default:return false;}}
bool DecodeTile(const std::vector<uint8_t>& d,uint32_t tag,std::vector<uint8_t>& raw,int kind){
 if(d.size()<32||std::memcmp(d.data(),"FB5TILE\0",8)||kind<0||kind>1)return false;
 const uint32_t w=le(&d[8]),h=le(&d[12]),n=le(&d[16]),crc=le(&d[20]);
 if(w!=uint32_t(kind?432:208)||h!=uint32_t(kind?224:96)||le(&d[24])!=tag||le(&d[28])!=uint32_t(kind)||n!=d.size()-32||n>2*w*h)return false;
 raw.clear();raw.reserve(w*h);size_t i=32;
 while(i<d.size()){const uint8_t ctrl=d[i++];const size_t count=ctrl&128?(ctrl&127)+3:ctrl+1;
  if(raw.size()+count>w*h)return false;
  if(ctrl&128){if(i==d.size())return false;raw.insert(raw.end(),count,d[i++]);}
  else{if(i+count>d.size())return false;raw.insert(raw.end(),d.begin()+i,d.begin()+i+count);i+=count;}}
 return raw.size()==w*h&&Crc32(raw.data(),raw.size())==crc;
}
cJSON* SpecJson(const Spec& s){auto* j=cJSON_CreateObject();cJSON_AddStringToObject(j,"font",FontIds[s.font]);cJSON_AddNumberToObject(j,"px",s.px);cJSON_AddStringToObject(j,"algorithm",Algorithms[s.algorithm]);cJSON_AddNumberToObject(j,"threshold",s.threshold);cJSON_AddNumberToObject(j,"weight",s.weight);cJSON_AddStringToObject(j,"content",Contents[s.content]);cJSON_AddBoolToObject(j,"inverse",s.inverse);cJSON_AddNumberToObject(j,"input_bpp",s.bpp);cJSON_AddStringToObject(j,"output",Outputs[s.output]);return j;}
int Session::Get(const Spec& s,int f)const{switch(f){case 0:return s.font;case 1:return s.px;case 2:return s.algorithm;case 3:return s.threshold;case 4:return s.weight;case 5:return s.content;case 6:return s.inverse;case 7:return s.bpp;case 10:return s.output;default:return -1;}}
void Session::Set(Spec& s,int f,int v){switch(f){case 0:s.font=v;break;case 1:s.px=v;break;case 2:s.algorithm=v;break;case 3:s.threshold=v;break;case 4:s.weight=v;break;case 5:s.content=v;break;case 6:s.inverse=v;break;case 7:s.bpp=v;break;case 10:s.output=v;break;default:break;}}
bool Session::Has(int f,int w)const{return f>=0&&f<FontCount&&wi(w)>=0&&!sources_[f][wi(w)].empty();}
int Session::ResolveWeight(int f,int requested)const{
 if(Has(f,requested))return requested;
 int best=-1,distance=100000;
 for(const int candidate:{400,500,700})if(Has(f,candidate)){
  const int d=std::abs(candidate-requested);
  if(d<distance){distance=d;best=candidate;}
 }
 return best;
}
void Session::ResetAxis(){values_.clear();if(axis_==0){for(int f=0;f<FontCount&&values_.size()<4;++f)if(Has(f,base_.weight))values_.push_back(f);}else if(axis_==1){for(int v:{base_.px-2,base_.px,base_.px+2,base_.px+4})if(Valid(1,v))values_.push_back(v);}else if(axis_==2)values_={0,1,2,3};else if(axis_==10)values_={0,1};else values_={144,128,112};
 const int v=Get(base_,axis_);if(std::find(values_.begin(),values_.end(),v)==values_.end()){values_.push_back(v);if(values_.size()>4)values_.erase(values_.begin());}}
bool Session::Enter(){
 ready_=false;pending_=true;controls_.clear();cards_.clear();sources_={};pinnedValid_=false;std::vector<uint8_t> raw;
 if(!read(directory_+"/manifest.json",262144,raw)){error_="COPY SD /inkdesk/fontbench";return false;}
 raw.push_back(0);Json root(cJSON_ParseWithLengthOpts(reinterpret_cast<const char*>(raw.data()),raw.size(),nullptr,1),cJSON_Delete);
 int schema=0;if(!root||!integer(item(root.get(),"schema"),6,6,schema)||!u32(item(root.get(),"bundle_tag"),tag_)){error_="INVALID BANK MANIFEST";return false;}
 const auto* catalog=item(root.get(),"catalog");const auto* ctrls=item(root.get(),"controls");
 if(!cJSON_IsArray(catalog)||cJSON_GetArraySize(catalog)!=FontCount||!cJSON_IsArray(ctrls)||(cJSON_GetArraySize(ctrls)<59||cJSON_GetArraySize(ctrls)>80)){error_="INVALID CATALOG OR CONTROLS";return false;}
 for(int fi=0;fi<FontCount;++fi){const auto* c=cJSON_GetArrayItem(catalog,fi);if(str(item(c,"id"))!=FontIds[fi]){error_="FONT ID ORDER MISMATCH";return false;}
  const auto* faces=item(c,"faces");if(!cJSON_IsArray(faces)||cJSON_GetArraySize(faces)>3){error_="INVALID FACES";return false;}
  for(int k=0;k<cJSON_GetArraySize(faces);++k){const auto* f=cJSON_GetArrayItem(faces,k);int weight=0;
   const auto* variable=item(item(f,"axes"),"wght");const auto* w=variable?variable:item(f,"weight");
   if(!integer(w,400,700,weight)||wi(weight)<0){error_="INVALID WEIGHT";return false;}
   const auto sha=str(item(f,"sha256"));if(sha.size()!=64||sha.find_first_not_of("0123456789abcdef")!=std::string::npos||!sources_[fi][wi(weight)].empty()){error_="INVALID FONT IDENTITY";return false;}
   sources_[fi][wi(weight)]=sha;
  }}
 for(int i=0;i<cJSON_GetArraySize(ctrls);++i){const auto* o=cJSON_GetArrayItem(ctrls,i);Control c{};std::string field=str(item(o,"field"));c.field=-1;
  for(int f=0;f<11;++f)if(field==Fields[f])c.field=f;
  if(!integer(item(o,"value"),0,700,c.value)||!Valid(c.field,c.value)){error_="INVALID CONTROL";return false;}
  const auto* b=item(o,"box");int coords[4];if(!cJSON_IsArray(b)||cJSON_GetArraySize(b)!=4){error_="INVALID RECT";return false;}
  for(int k=0;k<4;++k)if(!integer(cJSON_GetArrayItem(b,k),0,800,coords[k])){error_="INVALID RECT";return false;}
  c.box={coords[0],coords[1],coords[2],coords[3]};if(c.box.w<1||c.box.h<1||c.box.x+c.box.w>480||c.box.y+c.box.h>800){error_="CONTROL OUTSIDE SCREEN";return false;}controls_.push_back(c);
 }
 uint32_t crc=0;if(!u32(item(root.get(),"controls_crc32"),crc)||!read(directory_+"/controls.i1",FrameBytes,template_)||template_.size()!=FrameBytes||Crc32(template_.data(),template_.size())!=crc){error_="CONTROL PICTURE MISMATCH";return false;}
 // Prefer a real available font at initial entry; identity is visible, never per-sample fallback.
 if(!Has(base_.font,base_.weight)){for(int f=0;f<FontCount;++f)if(Has(f,400)){base_.font=f;base_.weight=400;break;}}
 ready_=true;error_.clear();full_=true;ResetAxis();return Compose();
}
bool Session::Compose(){
 if(!ready_)return false;
 pixels_=template_;cards_.clear();selected_=0;pending_=true;displayVerified_=false;++revision_;
 for(const auto& c:controls_){bool on=c.field==8?axis_==c.value:c.field==axis_?std::find(values_.begin(),values_.end(),c.value)!=values_.end():Get(base_,c.field)==c.value;if(c.field==3&&(base_.output||base_.algorithm<2||base_.algorithm==6)){on=false;for(int x=c.box.x+4;x<c.box.x+c.box.w-4;++x)pixel(pixels_,x,c.box.y+c.box.h/2,false);}if(on)invert(pixels_,c.box);}
 std::vector<Spec> specs;if(pinnedValid_)specs.push_back(pinned_);for(int v:values_){Spec s=base_;Set(s,axis_,v);if(pinnedValid_&&s==pinned_)continue;if(specs.size()<4)specs.push_back(s);}
 if(calibration_)specs.assign(4,base_);
 for(size_t i=0;i<specs.size();++i){Card card;card.spec=specs[i];card.box={22+int(i%2)*228,496+int(i/2)*122,208,96};card.baseline=pinnedValid_&&i==0;
  const auto& q=card.spec;std::vector<uint8_t> enc,raw;
  if(calibration_){
   const uint8_t shades[]={255,170,85,0};card.spec.output=1;card.luminance.assign(208*96,shades[i]);
   for(int y=0;y<96;++y)for(int x=0;x<208;++x)pixel(pixels_,card.box.x+x,card.box.y+y,shades[i]>=128);
   card.id="CALIBRATION-"+std::to_string(i);card.error="COLOR BLOCK / NOT A FONT VOTE";card.crc=Crc32(card.luminance.data(),card.luminance.size());cards_.push_back(card);continue;
  }
  if(q.algorithm==6){
   card.firmware=true;card.error="ORIGINAL UI / EXACT SIZES ONLY";
   // Live adapter resolves original ui_font_aXX, checks every glyph, then captures
   // its actual LVGL result. No generic Font() mapping or fallback here.
   cards_.push_back(card);continue;
  }
  char file[80];std::snprintf(file,sizeof(file),"/tiles/%d-%d-%d-%d-%d-0.t5",q.font,q.weight,q.px,q.algorithm,q.content);
  card.valid=Has(q.font,q.weight)&&read(directory_+file,2*208*96+32,enc)&&DecodeTile(enc,tag_,raw);
  if(card.valid){card.sha=sources_[q.font][wi(q.weight)];card.masterCrc=Crc32(raw.data(),raw.size());card.luminance.resize(raw.size());
   for(int y=0;y<96;++y)for(int x=0;x<208;++x){int v=raw[y*208+x];if(q.bpp!=8){const int levels=(1<<q.bpp)-1;v=(((v*levels+127)/255)*255+levels/2)/levels;}
    uint8_t luma;
    if(q.output){const int cover=(v*3+127)/255;luma=uint8_t((q.inverse?cover:3-cover)*85);}
    else {const bool ink=v>=q.threshold;luma=(q.inverse?ink:!ink)?255:0;}
    card.luminance[y*208+x]=luma;pixel(pixels_,card.box.x+x,card.box.y+y,luma>=128);
   }
   card.crc=Crc32(card.luminance.data(),card.luminance.size());char id[160];std::snprintf(id,sizeof(id),"%lu:%d-%d-%d-%d-%d-0.t5:%d:%d:%d:%d",static_cast<unsigned long>(tag_),q.font,q.weight,q.px,q.algorithm,q.content,q.threshold,q.bpp,q.inverse,q.output);card.id=id;
  }else{card.error=Has(q.font,q.weight)?"SAMPLE INVALID":"FONT RESOURCE MISSING";
   for(int k=0;k<40;++k){pixel(pixels_,card.box.x+84+k,card.box.y+24+k,false);pixel(pixels_,card.box.x+123-k,card.box.y+24+k,false);}}
  cards_.push_back(card);
 }

 return true;
}
namespace {
bool parseSpec(const cJSON* o,Spec& s){
 if(!cJSON_IsObject(o))return false;
 const auto f=str(item(o,"font")),a=str(item(o,"algorithm")),t=str(item(o,"content"));
 s.font=s.algorithm=s.content=-1;
 for(int i=0;i<FontCount;++i)if(f==FontIds[i])s.font=i;
 for(int i=0;i<AlgorithmCount;++i)if(a==Algorithms[i])s.algorithm=i;
 for(int i=0;i<4;++i)if(t==Contents[i])s.content=i;
 const auto* output=item(o,"output");s.output=0;if(output){const auto v=str(output);if(v!="mono"&&v!="gray4")return false;s.output=v=="gray4";}
 int inv=0;if(!cJSON_IsBool(item(o,"inverse")))return false;inv=cJSON_IsTrue(item(o,"inverse"));s.inverse=inv;
 return Valid(0,s.font)&&Valid(2,s.algorithm)&&Valid(5,s.content)&&
  integer(item(o,"px"),16,40,s.px)&&integer(item(o,"weight"),400,700,s.weight)&&Valid(4,s.weight)&&
  integer(item(o,"threshold"),112,144,s.threshold)&&Valid(3,s.threshold)&&
  integer(item(o,"input_bpp"),2,8,s.bpp)&&Valid(7,s.bpp);
}
}
bool ParseConfig(const cJSON* j,Config& c){
 c.full=true;if(const auto* r=item(j,"refresh")){const auto v=str(r);if(v!="full"&&v!="fast")return false;c.full=v=="full";}
 if(!parseSpec(item(j,"spec"),c.spec)||(!integer(item(j,"axis"),0,10,c.axis)||!Valid(8,c.axis)))return false;
 const auto* vals=item(j,"values");if(!cJSON_IsArray(vals))return false;c.count=cJSON_GetArraySize(vals);
 if(c.count<1||c.count>4)return false;
 for(int i=0;i<c.count;++i){if(!integer(cJSON_GetArrayItem(vals,i),0,700,c.values[i])||!Valid(c.axis,c.values[i]))return false;
  for(int k=0;k<i;++k)if(c.values[k]==c.values[i])return false;}
 const auto* pin=item(j,"pin_spec");c.hasPin=pin!=nullptr;
 if(c.hasPin){if(!parseSpec(pin,c.pin))return false;const auto sha=str(item(j,"pin_sha256"));
  if(sha.size()!=64||sha.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;
  std::memcpy(c.pinSha.data(),sha.c_str(),65);}
 return true;
}
bool Session::Apply(const Config& c){
 if(!ready_&&!Enter())return false;
 // Validate again: core callers do not have to come through JSON.
 if(!Valid(0,c.spec.font)||!Valid(1,c.spec.px)||!Valid(2,c.spec.algorithm)||!Valid(3,c.spec.threshold)||
    !Valid(4,c.spec.weight)||!Valid(5,c.spec.content)||!Valid(6,c.spec.inverse)||!Valid(7,c.spec.bpp)||!Valid(10,c.spec.output)||!Valid(8,c.axis)||c.count<1||c.count>4)return false;
 for(int i=0;i<c.count;++i){if(!Valid(c.axis,c.values[i]))return false;for(int k=0;k<i;++k)if(c.values[k]==c.values[i])return false;}
 if(c.hasPin&&c.pin.algorithm!=6&&(!Valid(0,c.pin.font)||!Valid(1,c.pin.px)||!Valid(2,c.pin.algorithm)||!Valid(3,c.pin.threshold)||!Valid(4,c.pin.weight)||!Valid(5,c.pin.content)||!Valid(6,c.pin.inverse)||!Valid(7,c.pin.bpp)||!Valid(10,c.pin.output)||!Has(c.pin.font,c.pin.weight)||c.pinSha.back()!=0||sources_[c.pin.font][wi(c.pin.weight)]!=c.pinSha.data())){error_="BASELINE FONT VERSION MISMATCH";return false;}
 if(c.hasPin&&c.pin.algorithm==6&&(c.pin.font!=0||c.pin.weight!=500||c.pin.output!=0||c.pin.threshold!=128||c.pin.bpp!=2||!Valid(1,c.pin.px)||!Valid(5,c.pin.content)||!Valid(6,c.pin.inverse)||c.pinSha.back()!=0||std::strlen(BaselineAssetSha)!=64||std::strcmp(c.pinSha.data(),BaselineAssetSha)))return false;
 Spec resolved=c.spec;const int resolvedWeight=ResolveWeight(resolved.font,resolved.weight);if(resolvedWeight<0){error_="FONT RESOURCE MISSING";return false;}
 const bool weightChanged=resolved.weight!=resolvedWeight;resolved.weight=resolvedWeight;
 calibration_=false;base_=resolved;axis_=c.axis;values_.assign(c.values.begin(),c.values.begin()+c.count);pinnedValid_=c.hasPin;if(c.hasPin)pinned_=c.pin;
 full_=c.full;error_.clear();notice_=weightChanged?"WEIGHT RESOLVED TO AVAILABLE FACE":"ALL PARAMETERS / ONE REFRESH";return Compose();
}
Action Session::ControlValue(int field,int value,bool toggle){
 if(!Valid(field,value))return Action::None;
 if(field==9&&value==3&&!ready_){Enter();return Action::Redraw;}if(!ready_)return Action::None;
 error_.clear();
 if(field==9){
  if(value==5){calibration_=!calibration_;full_=true;notice_="GRAY BLOCKS / ENABLE REQUIRED";Compose();return Action::Redraw;}
  if(value==6){grayArmed_=true;full_=true;notice_="EXPERIMENT ARMED / NOT OPTICALLY VERIFIED";Compose();return Action::Redraw;}
  if(value==7){error_.clear();grayArmed_=false;calibration_=false;recovery_=true;base_.output=0;pinnedValid_=false;full_=true;notice_="RESTORE ORIGINAL DISPLAY";Compose();return Action::Redraw;}
  if(value==0){if(selected_>=cards_.size()||!cards_[selected_].valid||pending_){notice_="SELECT VALID SAMPLE";return Action::None;}pinned_=cards_[selected_].spec;pinnedValid_=true;notice_="BASELINE A PINNED";}
  if(value==1){pinnedValid_=false;notice_="BASELINE RELEASED";}
  if(value==2){if(pending_||selected_>=cards_.size()||!cards_[selected_].valid){notice_="CANNOT RATE INVALID SAMPLE";return Action::None;}return Action::Vote;}
  full_=value!=4;if(value==3)notice_="FULL REQUEST";if(value==4)notice_="FAST REQUEST";
  Compose();return Action::Redraw;
 }
 full_=true;calibration_=false;notice_="TAP PARAMETERS / NO AUTO REFRESH";
 if(field==8){axis_=value;ResetAxis();}
 else{Set(base_,field,value);
  if(field==0||field==4){const int requested=base_.weight;const int resolved=ResolveWeight(base_.font,requested);if(resolved<0){error_="FONT RESOURCE MISSING";notice_="FONT RESOURCE REQUIRED";return Action::None;}if(resolved!=requested){base_.weight=resolved;notice_="WEIGHT RESOLVED TO AVAILABLE FACE";}}
  if(field==2&&value==6){base_.font=0;base_.weight=500;base_.threshold=128;base_.bpp=2;base_.output=0;}
if(field==axis_){auto it=std::find(values_.begin(),values_.end(),value);
    if(toggle&&it!=values_.end()&&values_.size()>1)values_.erase(it);
    else if(it==values_.end()){values_.push_back(value);if(values_.size()>4)values_.erase(values_.begin());}
    if(!toggle)values_={value};}}
 Compose();return Action::Redraw;
}
Action Session::Tap(int x,int y){if(x<0||x>=Width||y<0||y>=Height)return Action::None;if(y<40&&x<220)return Action::Exit;
 if(!ready_){if(y>=719&&y<758&&x>=287&&x<370){Enter();return Action::Redraw;}return Action::None;}
 for(const auto& c:controls_)if(c.box.contains(x,y)){if(c.field==3&&(base_.output||base_.algorithm<2||base_.algorithm==6))return Action::None;return ControlValue(c.field,c.value,true);}
 for(size_t i=0;i<cards_.size();++i){auto box=cards_[i].box;box.y-=24;box.h+=24;if(box.contains(x,y)){selected_=i;notice_=cards_[i].valid?"SAMPLE SELECTED":cards_[i].error;
   full_=true;pending_=true;++revision_;return Action::Redraw;}}
 return Action::None;
}
void Session::Painted(bool full,uint32_t rev){Json receipt(cJSON_Parse(displayReceipt_.c_str()),cJSON_Delete);if(receipt&&cJSON_IsTrue(item(receipt.get(),"cleaned")))full=true;pending_=false;lastFull_=full;frameRevision_=rev;if(full){fastCount_=0;++fullCount_;}else ++fastCount_;}
void Session::PaintFailed(){pending_=false;displayVerified_=false;notice_="DISPLAY ERROR / RETRY FULL";}
bool Session::LegacyPage(unsigned page){if(page>=4096)return false;const bool ok=ready_?Compose():Enter();notice_="OLD ENTRY -> UNIFIED PANEL";return ok;}
bool Session::SaveVote(const std::string& path,const char* boot,uint32_t uptime){
 if(pending_||selected_>=cards_.size()||!cards_[selected_].valid||!displayVerified_||calibration_||(cards_[selected_].spec.output&&!grayArmed_))return false;
 const auto& card=cards_[selected_];
 Json j(cJSON_CreateObject(),cJSON_Delete);cJSON_AddNumberToObject(j.get(),"schema",6);cJSON_AddStringToObject(j.get(),"build","1.0.0-fontbench6");cJSON_AddStringToObject(j.get(),"feedback","优选");cJSON_AddStringToObject(j.get(),"evidence","device-human-vote");cJSON_AddStringToObject(j.get(),"boot_id",boot?boot:"");cJSON_AddNumberToObject(j.get(),"uptime_ms",uptime);cJSON_AddNumberToObject(j.get(),"frame_revision",frameRevision_);cJSON_AddNumberToObject(j.get(),"bundle_tag",tag_);cJSON_AddStringToObject(j.get(),"sample_id",card.id.c_str());cJSON_AddStringToObject(j.get(),"font_sha256",card.sha.c_str());cJSON_AddItemToObject(j.get(),"spec",SpecJson(card.spec));cJSON_AddNumberToObject(j.get(),"region_crc32",card.crc);cJSON_AddNumberToObject(j.get(),"fast_refresh_count",fastCount_);cJSON_AddBoolToObject(j.get(),"full_refresh",lastFull_);cJSON_AddBoolToObject(j.get(),"resolved_ok",true);cJSON_AddBoolToObject(j.get(),"fallback_used",false);
 cJSON_AddStringToObject(j.get(),"display_mode",displayMode_.c_str());
 cJSON_AddStringToObject(j.get(),"physical_quality","human-vote-only");
 if(auto* receipt=cJSON_Parse(displayReceipt_.c_str()))cJSON_AddItemToObject(j.get(),"display_receipt",receipt);
 char* text=cJSON_PrintUnformatted(j.get());if(!text)return false;const size_t n=std::strlen(text);struct stat st{};const int statResult=stat(path.c_str(),&st);
 if((statResult==0&&(st.st_size<0||uint64_t(st.st_size)+n+1>262144))||(statResult!=0&&errno!=ENOENT)){cJSON_free(text);notice_="LOG FULL / UNAVAILABLE";return false;}
 FILE* f=std::fopen(path.c_str(),"ab");bool ok=false;if(f){ok=std::fwrite(text,1,n,f)==n&&std::fputc('\n',f)!=EOF;ok=std::fclose(f)==0&&ok;}cJSON_free(text);notice_=ok?"SAVED TO SD":"SAVE FAILED";return ok;
}
cJSON* Session::Status(bool compact)const{
 auto* j=cJSON_CreateObject();cJSON_AddStringToObject(j,"firmware_asset_sha",BaselineAssetSha);cJSON_AddBoolToObject(j,"ready",ready_);cJSON_AddBoolToObject(j,"pending",pending_);cJSON_AddNumberToObject(j,"revision",revision_);cJSON_AddNumberToObject(j,"frame_revision",frameRevision_);cJSON_AddNumberToObject(j,"bundle_tag",tag_);cJSON_AddStringToObject(j,"error",error_.c_str());cJSON_AddStringToObject(j,"notice",notice_.c_str());cJSON_AddItemToObject(j,"spec",SpecJson(base_));cJSON_AddStringToObject(j,"compare_axis",Fields[axis_]);cJSON_AddNumberToObject(j,"selected",selected_);cJSON_AddNumberToObject(j,"output_bpp",WantsGray()&&grayArmed_?2:1);cJSON_AddBoolToObject(j,"gray_armed",grayArmed_);cJSON_AddBoolToObject(j,"calibration",calibration_);cJSON_AddStringToObject(j,"display_mode",displayMode_.c_str());cJSON_AddBoolToObject(j,"digital_commit",displayVerified_);cJSON_AddStringToObject(j,"physical_quality","NOT_PROVEN");if(auto* receipt=cJSON_Parse(displayReceipt_.c_str()))cJSON_AddItemToObject(j,"display_receipt",receipt);cJSON_AddNumberToObject(j,"fast_refresh_count",fastCount_);cJSON_AddBoolToObject(j,"last_full",lastFull_);
 if(!compact){auto* arr=cJSON_AddArrayToObject(j,"cards");for(const auto& card:cards_){auto* c=cJSON_CreateObject();cJSON_AddItemToObject(c,"spec",SpecJson(card.spec));cJSON_AddStringToObject(c,"sample_id",card.id.c_str());cJSON_AddStringToObject(c,"font_sha256",card.sha.c_str());cJSON_AddNumberToObject(c,"region_crc32",card.crc);cJSON_AddNumberToObject(c,"master_crc32",card.masterCrc);cJSON_AddNumberToObject(c,"requested_px",card.spec.px);cJSON_AddNumberToObject(c,"resolved_px",card.valid?card.spec.px:0);cJSON_AddBoolToObject(c,"resolved_ok",card.valid);cJSON_AddBoolToObject(c,"firmware_original",card.firmware);cJSON_AddBoolToObject(c,"threshold_effective",card.spec.output==0&&card.spec.algorithm>=2&&card.spec.algorithm!=6);cJSON_AddBoolToObject(c,"eligible",card.valid&&displayVerified_&&(!card.spec.output||grayArmed_)&&!calibration_);auto* bounds=cJSON_AddArrayToObject(c,"box");for(int v:{card.box.x,card.box.y,card.box.w,card.box.h})cJSON_AddItemToArray(bounds,cJSON_CreateNumber(v));cJSON_AddBoolToObject(c,"fallback_used",false);cJSON_AddBoolToObject(c,"baseline",card.baseline);cJSON_AddStringToObject(c,"error",card.error.c_str());cJSON_AddItemToArray(arr,c);}}
 return j;
}
bool Session::WantsGray()const{if(calibration_)return true;for(const auto& c:cards_)if(c.spec.output&&c.valid)return true;return false;}
void Session::FirmwareResolved(size_t i,bool ok,const char* sha){
 if(i>=cards_.size()||!cards_[i].firmware)return;
 auto& c=cards_[i];c.valid=ok;
 if(ok){c.sha=sha?sha:"";c.error.clear();c.id="FW-UI:"+c.sha+":"+std::to_string(c.spec.px)+":"+std::to_string(c.spec.content)+":"+std::to_string(c.spec.inverse);}
 else c.error="ORIGINAL UI SIZE / GLYPH UNAVAILABLE";
}
void Session::CaptureFirmware(const uint8_t* native,size_t bytes){
 if(!native||bytes!=FrameBytes)return;
 for(auto& c:cards_)if(c.firmware&&c.valid){c.luminance.resize(c.box.w*c.box.h);
  for(int y=0;y<c.box.h;++y)for(int x=0;x<c.box.w;++x){const int nx=c.box.y+y,ny=479-c.box.x-x;const size_t bit=ny*800+nx;c.luminance[y*c.box.w+x]=(native[bit/8]&(128u>>(bit&7)))?255:0;}
  c.crc=Crc32(c.luminance.data(),c.luminance.size());c.masterCrc=c.crc;
 }
}
void Session::PutGrayRegions(uint8_t* native,size_t size)const{
 if(!native||size!=Width*Height||!grayArmed_)return;
 for(const auto& c:cards_)if(c.spec.output&&c.luminance.size()==size_t(c.box.w*c.box.h))
  for(int y=0;y<c.box.h;++y)for(int x=0;x<c.box.w;++x)native[(479-c.box.x-x)*800+c.box.y+y]=c.luminance[y*c.box.w+x];
}
void Session::RecordDisplay(const char* mode,const char* receipt,bool successful){if(successful){if(error_.find("DISPLAY FAULT")==0)error_.clear();}else error_="DISPLAY FAULT / TAP RESTORE";displayMode_=mode?mode:"unknown";displayReceipt_=receipt?receipt:"{}";displayVerified_=successful;}

}
