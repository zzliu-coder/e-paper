#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "cJSON.h"
namespace paper::fontbench {
constexpr int Width=480,Height=800,FrameBytes=48000,FontCount=8;
inline constexpr const char* FontIds[]={"sourcehan","misans","harmony","lxgw","wqy","pingfang","heiti","noto"};
inline constexpr const char* Algorithms[]={"M-A","M-N","N-A","L-A","N-N","O-N","FW"};
inline constexpr int AlgorithmCount=7;
inline constexpr const char* Outputs[]={"mono","gray4"};
inline constexpr const char* Contents[]={"common","dense","mixed","body"};
inline constexpr const char* Fields[]={"font","px","algorithm","threshold","weight","content","inverse","input_bpp","axis","action","output"};
struct Rect {int x=0,y=0,w=0,h=0;bool contains(int a,int b)const{return a>=x&&b>=y&&a<x+w&&b<y+h;}};
struct Spec {int font=7,px=22,algorithm=2,threshold=128,weight=400,content=0,inverse=0,bpp=8,output=0;
 bool operator==(const Spec& b)const{return font==b.font&&px==b.px&&algorithm==b.algorithm&&threshold==b.threshold&&weight==b.weight&&content==b.content&&inverse==b.inverse&&bpp==b.bpp&&output==b.output;}};
struct Config {Spec spec;bool full=true;int axis=2,count=0;std::array<int,4> values{};bool hasPin=false;Spec pin;std::array<char,65> pinSha{};};
bool ParseConfig(const cJSON*,Config&);
struct Card {Spec spec;Rect box;bool valid=false,baseline=false,firmware=false;std::vector<uint8_t> luminance;uint32_t crc=0,masterCrc=0;std::string sha,id,error;};
struct Control {int field,value;Rect box;};
enum class Action {None,Redraw,Exit,Vote};
uint32_t Crc32(const uint8_t* p,size_t n);
bool Valid(int field,int value);
bool DecodeTile(const std::vector<uint8_t>& encoded,uint32_t tag,std::vector<uint8_t>& raw,int kind=0);
cJSON* SpecJson(const Spec& spec);
class Session {
 public:
  explicit Session(std::string directory="/sdcard/inkdesk/fontbench"):directory_(std::move(directory)){}
  bool Enter();
  Action ControlValue(int field,int value,bool toggle=false);
  bool Apply(const Config&);
  Action Tap(int x,int y);
  Action Step(int){return Action::None;} // No previous/next-page navigation.
  void Painted(bool full,uint32_t frameRevision);
  void PaintFailed();
  bool WantsGray()const;
  bool grayArmed()const{return grayArmed_;}
  bool calibration()const{return calibration_;}
  bool TakeRecovery(){bool r=recovery_;recovery_=false;return r;}
  void FirmwareResolved(size_t i,bool ok,const char* sha);
  void CaptureFirmware(const uint8_t* native,size_t bytes);
  void PutGrayRegions(uint8_t* native,size_t size)const;
  void PutGrayRegions(std::vector<uint8_t>& native)const{PutGrayRegions(native.data(),native.size());}
  void RecordDisplay(const char* mode,const char* receipt,bool successful);
  const std::string& displayReceipt()const{return displayReceipt_;}

 bool SaveVote(const std::string& path,const char* boot,uint32_t uptime);
  bool fullRequested()const{return full_;}
  bool ready()const{return ready_;}
  bool pending()const{return pending_;}
  const std::string& error()const{return error_;}
  const std::string& notice()const{return notice_;}
  const Spec& spec()const{return base_;}
  const std::vector<Card>& cards()const{return cards_;}
  const std::vector<uint8_t>& pixels()const{return pixels_;}
  const std::vector<Control>& controls()const{return controls_;}
  unsigned selectedIndex()const{return selected_;}
  cJSON* Status(bool compact=false)const;
  // Old external page requests are redirected to this same panel, preserving the origin notice.
 bool LegacyPage(unsigned page);
 private:
  bool Compose();bool Has(int font,int weight)const;int ResolveWeight(int font,int requested)const;void ResetAxis();int Get(const Spec&,int)const;void Set(Spec&,int,int);
  std::string directory_,error_,notice_="I1 / TAP PARAMETERS";
  std::vector<Control> controls_;std::vector<uint8_t> template_,pixels_;
  std::array<std::array<std::string,3>,FontCount> sources_{};
  Spec base_,pinned_;bool pinnedValid_=false,ready_=false,pending_=true,full_=true,lastFull_=true;
  int axis_=2;std::vector<int> values_{0,1,2,3};std::vector<Card> cards_;unsigned selected_=0,fastCount_=0;
  bool grayArmed_=false,calibration_=false,recovery_=false,displayVerified_=false;
  std::string displayMode_="mono",displayReceipt_="{}";
  uint32_t tag_=0,revision_=0,frameRevision_=0,fullCount_=0;
};
}
