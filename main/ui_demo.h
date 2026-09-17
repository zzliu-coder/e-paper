#pragma once
#include "inkdesk_r2/core/types.h"

// Interactive device-native design sandbox. Never writes settings or documents.
namespace ui_demo {
enum class Page { Home, Library, Reading, Settings, Wallpaper, Detail, Fonts, Gallery, FontLab };
enum class Exit { None, Reader, Notes, Diagnostics, Legacy };
class Model {
 public:
  Page page=Page::Home;
  bool visible=true, dirty=true, controls=false, bookmarked=false;
  int font=25, readingPage=1, tab=0, wallpaper=0;
  int gallery=0;
  int labPage=0,labProfile=0,labSize=3,labFeedback=-1;
  uint32_t labVote=0;
  int labSaved=0; // 0 not saved, 1 SD receipt, -1 storage failure
  bool gallerySelected=false;
  int refreshRequest=0; // 0 auto, 1 explicit full, 2 explicit fast (calibration only).
  int theme=0; // Legacy reading-theme slot; the active lab contains black fonts only.
  uint32_t revision=1;
  const char* detail="";
  void Draw(inkdesk::Frame& f) const;
  Exit Tap(const inkdesk::Frame& visibleFrame,int x,int y);
  void Home();
  void Step(int direction);
  void Tools();
  int PageCount() const;
  size_t ReadingOffset() const;
  static const char* SampleText();
 private:
  void Changed() { ++revision; dirty=true; }
};
// Bitmap numerals are procedural UI primitives, not screenshots or image assets.
// Both the native LVGL painter and offline renderer use the same bit rows.
inline uint8_t DotRow(char c,int row) {
  static constexpr uint8_t digits[10][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
  if(row<0||row>=7)return 0;
  if(c>='0'&&c<='9')return digits[c-'0'][row];
  if(c==':')return row==2||row==4 ? 4:0;
  if(c=='/')return uint8_t(1<<std::min(4,row*5/7));
  return 0;
}
}
