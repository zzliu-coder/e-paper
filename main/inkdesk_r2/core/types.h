#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace inkdesk {
  constexpr int kWidth=480, kHeight=800;
  // All timestamps are monotonic uint32 milliseconds; subtraction tolerates wrap.
  inline uint32_t elapsed(uint32_t now, uint32_t then) {
    return now-then;
  }
  // Strict Unicode scalar decoding: reject overlong forms, surrogates and > U+10FFFF.
  inline size_t utf8Next(const char* s, size_t n, uint32_t& cp) {
    if (!n) return 0;
    auto b=static_cast<uint8_t>(s[0]);
    size_t len=0;
    if (b<0x80) {
      cp=b;
      return 1;
    }
    if (b>=0xC2 && b<=0xDF) {
      len=2;
      cp=b&31;
    }
    else if (b>=0xE0 && b<=0xEF) {
      len=3;
      cp=b&15;
    }
    else if (b>=0xF0 && b<=0xF4) {
      len=4;
      cp=b&7;
    }
    else return 0;
    if (n<len) return 0;
    for (size_t i=1;i<len;++i) {
      auto c=static_cast<uint8_t>(s[i]);
      if ((c&0xC0)!=0x80) return 0;
      cp=(cp<<6)|(c&63);
    }
    if ((len==2 && cp<0x80)||(len==3 && cp<0x800)||(len==4 && cp<0x10000)||
    (cp>=0xD800 && cp<=0xDFFF)||cp>0x10FFFF) return 0;
    return len;
  }
  inline bool validUtf8(const char* s,size_t n) {
    size_t i=0;
    while(i<n) {
      uint32_t cp=0;
      size_t z=utf8Next(s+i,n-i,cp);
      if(!z||!cp) return false;
      i+=z;
    } return true;
  }
  template<size_t N> class Text {
    std::array<char,N+1> data_ {};
    size_t length_=0;
    public:
    const char* c_str() const {
      return data_.data();
    } size_t size() const {
      return length_;
    }
    bool empty() const {
      return !length_;
    } static constexpr size_t capacity() {
      return N;
    }
    bool append(const char* s, size_t n) {
      if(!s||n>N-length_||!validUtf8(s,n)) return false;
      std::memmove(data_.data()+length_,s,n);
      length_+=n;
      data_[length_]=0;
      return true;
    }
    bool append(const char* s) {
      return s && append(s,std::strlen(s));
    }
    bool assign(const char* s, size_t n) {
      if(!s||n>N||!validUtf8(s,n))return false;
      std::memmove(data_.data(),s,n);
      length_=n;
      data_[n]=0;
      return true;
    }
    bool assign(const char* s) {
      return s && assign(s,std::strlen(s));
    }
    void clear() {
      length_=0;
      data_[0]=0;
    }
    bool pop() {
      if(!length_)return false;
      --length_;
      while(length_&&(static_cast<uint8_t>(data_[length_])&0xC0)==0x80)--length_;
      data_[length_]=0;
      return true;
    }
  };
  struct Rect {
    int x=0,y=0,w=0,h=0;
    bool empty()const {
      return w<=0||h<=0;
    }
    bool contains(int px,int py)const {
      return !empty()&&px>=x&&py>=y&&int64_t(px)-x<w&&int64_t(py)-y<h;
    }
  };
  inline Rect clip(Rect r) {
    int x=std::clamp(r.x,0,kWidth),y=std::clamp(r.y,0,kHeight);
    int ex=int(std::clamp<int64_t>(int64_t(r.x)+r.w,0,kWidth)),ey=int(std::clamp<int64_t>(int64_t(r.y)+r.h,0,kHeight));
    return {
      x,y,std::max(0,ex-x),std::max(0,ey-y)
    };
  }
  inline Rect unite(Rect a,Rect b) {
    a=clip(a);
    b=clip(b);
    if(a.empty())return b;
    if(b.empty())return a;
    int x=std::min(a.x,b.x),y=std::min(a.y,b.y);
    return {
      x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y
    };
  }
  constexpr Rect fullRect() {
    return {
      0,0,kWidth,kHeight
    };
  }
  template<class T,size_t N> class Queue {
    std::array<T,N> data_ {};
    size_t first_=0,size_=0;
    uint32_t rejected_=0;
    public:
    bool push(const T& t) {
      if(size_==N) {
        ++rejected_;
        return false;
      }data_[(first_+size_)%N]=t;
      ++size_;
      return true;
    }
    bool pop(T& t) {
      if(!size_)return false;
      t=data_[first_];
      first_=(first_+1)%N;
      --size_;
      return true;
    }
    size_t size()const {
      return size_;
    }uint32_t rejected()const {
      return rejected_;
    }
  };
  enum class Action:uint16_t {
    None,Home,Notes,Todo,Reader,Lab,Tools,Settings,NewNote,OpenNote,Save,Back,ToggleTodo,NewTodo,
    Letter,Backspace,Space,Enter,ToggleLanguage,Candidate,CandidateNext,InputCancel,
    FastTest,CleanTest,FullTest,Pattern,FontSize,Previous,Next,Export,Reserved
  };
  struct Event {
    Action action=Action::None;
    int value=0;
    uint32_t epoch=0,composition=0;
  };
  enum class View:uint8_t {
    Home,Notes,Editor,Todo,TodoEditor,Reader,Lab,Tools
  };
  enum class DrawKind:uint8_t {
    Text,Rect,Fill,Line,Panel,PanelFill,Dots,Texture,Dashed
  };
  struct Draw {
    DrawKind kind=DrawKind::Text;
    Rect box {};
    uint8_t size=24;
    int8_t fontProfile=-1; // -1 normal UI; 0..3 static lab fonts; 4/5 flash fontpack masks.
    bool black=true;
    uint8_t radius=8,stroke=1,dash=6,gap=4,density=25;
    Text<96> text;
  };
  struct Hit {
    Rect box;
    Event event;
  };
  struct Frame {
    uint32_t revision=0,epoch=0,composition=0;
    View view=View::Home;
    std::array<Draw,144> draws {};
    std::array<Hit,56> hits {};
    size_t drawCount=0,hitCount=0;
    bool overflow=false;
    void reset() {
      drawCount=hitCount=0;
      overflow=false;
    }
    bool draw(DrawKind k,Rect r,const char* text="",int size=24,bool black=true) {
      if(drawCount==draws.size()) {
        overflow=true;
        return false;
      }
      auto&d=draws[drawCount];
      d.kind=k;
      d.box=r;
      d.size=static_cast<uint8_t>(size);
      d.fontProfile=-1;
      d.black=black;
      d.radius=8;d.stroke=1;d.dash=6;d.gap=4;d.density=25;
      if(!d.text.assign(text)) {
        overflow=true;
        return false;
      }++drawCount;
      return true;
    }
    // Text is always clipped to its own layout box, on both host and device.
    // Logical font size remains a category; physical font metrics are backend-owned.
    void textIn(Rect box,const char*s,int size=24,bool black=true) {
      draw(DrawKind::Text, clip(box),s,size,black);
    }
    void text(int x,int y,const char*s,int size=24,bool black=true) {
      textIn({x,y,kWidth-x,size+9},s,size,black);
    }
    void rect(Rect r,bool fill=false,bool black=true) {
      draw(fill?DrawKind::Fill:DrawKind::Rect,r,"",24,black);
    }
    void line(int y) {
      draw(DrawKind::Line, {
        24,y,432,1
      });
    }
    void button(Rect r,const char* s,Action a,int value=0,bool primary=false,int size=24) {
      rect(r,primary);
      const int y = r.y + std::max(0, (r.h-size)/2);
      textIn({r.x+10,y,std::max(0,r.w-20),std::min(size+9,r.y+r.h-y)},s,size,!primary);
      if(hitCount==hits.size()) {
        overflow=true;
        return;
      }
      hits[hitCount++]= {
        r, {
          a,value,epoch,composition
        }
      };
    }
    // Compare only drawing commands. Revision/candidate/hit metadata may change
    // while pixels stay identical (for example a repeated no-op navigation key).
    bool sameDrawing(const Frame& other) const {
      if (overflow || other.overflow || drawCount != other.drawCount) return false;
      for (size_t i=0; i<drawCount; ++i) {
        const auto& a=draws[i]; const auto& b=other.draws[i];
        if (a.kind!=b.kind || a.size!=b.size || a.fontProfile!=b.fontProfile || a.black!=b.black ||
            a.box.x!=b.box.x || a.box.y!=b.box.y || a.box.w!=b.box.w || a.box.h!=b.box.h ||
            std::strcmp(a.text.c_str(),b.text.c_str())!=0) return false;
      }
      return true;
    }
    Event hit(int x,int y)const {
      for(size_t i=hitCount;i>0;--i)if(hits[i-1].box.contains(x,y))return hits[i-1].event;
      return {};
    }
  };
} // namespace inkdesk
