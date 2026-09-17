#include "desk.h"
#include <cstdio>
namespace inkdesk {
  namespace {
    void appButton(Frame& f,const char* id,Rect box,bool primary=false,int size=24) {
      size_t count=0;
      const auto* apps=appRegistry(count);
      for (size_t i=0;i<count;++i) {
        if (std::strcmp(apps[i].id,id)!=0) continue;
        Text<96> label;
        label.assign(apps[i].title);
        if (!apps[i].available) label.append(" · 预留");
        f.button(box,label.c_str(),apps[i].available?apps[i].action:Action::Reserved,0,primary,size);
        return;
      }
      f.overflow=true; // unknown registrations fail visibly in development tests
    }
    void caption(Frame&f,int y,const char*s) {
      f.text(24,y,s,20);
    }
    void header(Frame&f,const char*title,const char*sub,bool back=true) {
      f.text(24,18,"INKDESK / R2",18);
      f.text(324,18,"METALIO",18);
      f.line(56);
      f.text(24,84,title,32);
      f.text(24,130,sub,20);
      if(back)f.button( {
        360,76,96,48
      },"返回",Action::Back,0,false,22);
    }
    // Deterministic, scalar-safe character wrapping. No font files in the core.
    // Firmware painter clips each row and uses actual upstream font glyphs.
    size_t wrap(Frame* f,const char*s,int x,int y,int width,int size,size_t skip,size_t maxLines) {
      const size_t n=std::strlen(s);
      size_t i=0,rows=0;
      int used=0;
      Text<96>line;
      auto flush=[&]() {
        if(f&&rows>=skip&&rows<skip+maxLines)f->textIn({x,y+int(rows-skip)*(size+9),width,size+9},line.c_str(),size);
        ++rows;
        used=0;
        line.clear();
      };
      while(i<n) {
        uint32_t cp=0;
        size_t len=utf8Next(s+i,n-i,cp);
        if(!len)break;
        if(cp=='\n') {
          flush();
          i+=len;
          continue;
        }
        int advance=cp<128?(size*3/5):size;
        if(used+advance>width||line.size()+len>line.capacity())flush();
        line.append(s+i,len);
        used+=advance;
        i+=len;
      }
      if(!line.empty()||!rows)flush();
      return rows;
    }
    void tail(Frame&f,const char*s,int x,int y,int w,int size,size_t lines) {
      size_t n=wrap(nullptr,s,x,y,w,size,0,0);
      wrap(&f,s,x,y,w,size,n>lines?n-lines:0,lines);
    }
    void teaser(const char*s,Text<96>&out,size_t chars=14) {
      size_t i=0,n=std::strlen(s),count=0;
      out.clear();
      while(i<n&&count<chars) {
        uint32_t cp=0;
        size_t z=utf8Next(s+i,n-i,cp);
        if(!z||cp=='\n')break;
        if(!out.append(s+i,z))break;
        i+=z;
        ++count;
      }if(i<n)out.append("…");
      if(out.empty())out.assign("空白笔记");
    }
    void keyboard(Frame&f,const Pinyin&ime,bool chinese) {
      f.line(433);
      f.text(24,445,chinese?"拼音":"英文 / 数字",20);
      const char*raw=ime.raw();
      const size_t rawLen=std::strlen(raw);
      f.text(144,445,ime.empty()?(chinese?"逐字或输入完整词语":"直接输入"):(rawLen>24?raw+rawLen-24:raw),20);
      if(chinese) {
        for(size_t i=0;i<Pinyin::kPageSize;++i) {
          Text<48>w;
          bool exists=ime.candidate(i,w);
          if(exists)f.button( {
            24+int(i)*88,478,82,48
          },w.c_str(),Action::Candidate,int(i),false,20);
        }
        f.button( {
          382,478,74,48
        },"换页",Action::CandidateNext,0,false,20);
      }else {
        for(int i=0;i<10;++i) {
          char label[2]= {
            char('0'+i),0
          };
          f.button( {
            24+i*43,478,39,48
          },label,Action::Letter,'0'+i,false,22);
        }
      }
      static constexpr const char*rows[]= {
        "qwertyuiop","asdfghjkl","zxcvbnm"
      };
      for(int row=0;row<3;++row) {
        int count=int(std::strlen(rows[row])),left=24+(10-count)*21;
        for(int col=0;col<count;++col) {
          char label[2]= {
            rows[row][col],0
          };
          f.button( {
            left+col*43,540+row*55,39,49
          },label,Action::Letter,rows[row][col],false,24);
        }
      }
      f.button( {
        24,708,69,52
      },chinese?"中/英":"英/中",Action::ToggleLanguage,0,false,18);
      f.button( {
        99,708,69,52
      },"清空",Action::InputCancel,0,false,20);
      f.button( {
        174,708,111,52
      },"空格",Action::Space,0,false,22);
      f.button( {
        291,708,76,52
      },"换行",Action::Enter,0,false,22);
      f.button( {
        373,708,83,52
      },"退格",Action::Backspace,0,false,22);
    }
  }
  void Desk::draw(Frame&f)const {
    f.reset();
    f.revision=revision_;
    f.epoch=epoch_;
    f.composition=ime_.revision();
    f.view=view_;
    switch(view_) {
      case View::Home:
      header(f,"纸间", "记下来，再慢慢读。",false);
      appButton(f,"notes",{24,186,208,126},true,28);
      appButton(f,"reader",{248,186,208,126},false,28);
      appButton(f,"todo",{24,332,432,92},false,26);
      appButton(f,"display",{24,444,208,88},false,24);
      appButton(f,"tools",{248,444,208,88},false,24);
      appButton(f,"settings",{24,552,432,72},false,24);
      f.line(660);
      caption(f,683,"本地优先 · 不自动联网 · 无逐帧动画");
      caption(f,719,"核心模块可单独测试和替换");
      break;
      case View::Notes: {
        header(f,"记事本","6 篇笔记 · 每篇最多 1536 字节");
        f.button( {
          24,171,432,56
        },"新建笔记",Action::NewNote,0,true);
        if(!docs_.noteCount)caption(f,264,"还没有笔记，先记下一句话。");
        for(size_t i=listPage_*5;i<docs_.noteCount&&i<listPage_*5+5;++i) {
          Text<96>t;
          teaser(docs_.notes[i].body.c_str(),t);
          f.button( {
            24,246+int(i-listPage_*5)*79,432,68
          },t.c_str(),Action::OpenNote,int(i),false,24);
        }
        f.button( {
          24,668,92,48
        },"上页",Action::Previous,0,false,20);
        f.button( {
          128,668,92,48
        },"下页",Action::Next,0,false,20);
        f.button( {
          244,668,212,48
        },"导出 Markdown",Action::Export,0,false,20);
        break;
      }
      case View::Todo: {
        header(f,"待办事项","轻量列表 · 最多 16 项");
        f.button( {
          24,171,432,56
        },"添加待办",Action::NewTodo,0,true);
        if(!docs_.todoCount)caption(f,264,"先写一件今天要做的事。");
        for(size_t i=listPage_*5;i<docs_.todoCount&&i<listPage_*5+5;++i) {
          Text<96>t;
          teaser(docs_.todos[i].text.c_str(),t,12);
          Text<96>label;
          label.append(docs_.todos[i].done?"[x] ":"[ ] ");
          label.append(t.c_str());
          f.button( {
            24,246+int(i-listPage_*5)*79,432,68
          },label.c_str(),Action::ToggleTodo,int(i),false,24);
        }
        f.button( {
          24,668,92,48
        },"上页",Action::Previous,0,false,20);
        f.button( {
          128,668,92,48
        },"下页",Action::Next,0,false,20);
        f.button( {
          244,668,212,48
        },"保存",Action::Save,0,false,22);
        break;
      }
      case View::Editor:case View::TodoEditor: {
        bool todo=view_==View::TodoEditor;
        f.text(24,18,"INKDESK / INPUT",18);
        f.line(56);
        f.text(24,83,todo?"新建待办":"编辑笔记",28);
        f.button( {
          258,73,92,48
        },"返回",Action::Back,0,false,22);
        f.button( {
          364,73,92,48
        },"保存",Action::Save,0,true,22);
        const char*text=todo?todoDraft_.c_str():docs_.notes[selectedNote_].body.c_str();
        if(!*text)f.text(24,159,"在这里留下一点想法…",24);
        else tail(f,text,24,159,432,24,7);
        f.line(401);
        f.text(24,408,!ime_.empty()?"拼音待选字 · 尚未写入正文":todo?"保存后加入列表":dirty()?"未保存 · 停笔后自动保存":"已保存",18);
        keyboard(f,ime_,chinese_);
        break;
      }
      case View::Reader:
      header(f,"电子书","复用已固定版本的 CrossMux 阅读器");
      wrap(&f,"实机入口会打开原有书库。EPUB 排版、字体、灰阶、翻页与阅读进度，继续由已有阅读器处理。",24,200,432,26,0,8);
      f.line(446);
      wrap(&f,"电脑预览只测试入口交接，不运行 EPUB 引擎，也不模拟墨水粒子的物理效果。",24,478,432,24,0,7);
      f.button( {
        24,683,432,58
      },"返回首页",Action::Home,0,true,24);
      break;
      case View::Lab: {
        header(f,"显示测试","同一内容，比较速度与残影");
        if(pattern_==0) {
          f.text(24,190,"中文清晰度 · 0123456789",fontSize_);
          f.text(24,245,"横竖撇捺，山水之间。",fontSize_);
          f.text(24,300,"Aa Bb Cc / 细线与粗线",fontSize_);
          for(int i=0;i<8;++i)f.rect( {
            24,365+i*7,432,i%3+1
          },true);
        }
        else if(pattern_==1) {
          for(int y=0;y<6;++y)for(int x=0;x<10;++x)if((x+y)%2==0)f.rect( {
            24+x*43,181+y*43,42,42
          },true);
        }
        else {
          f.rect( {
            24,183,432,270
          },true);
          f.text(46,239,"黑底白字",32,false);
          f.text(46,295,"残影观察页",32,false);
          f.text(46,346,"离开后观察旧图痕迹",22,false);
        }
        f.button( {
          24,485,208,54
        },"换测试图",Action::Pattern,0,false,24);
        f.button( {
          248,485,208,54
        },"切换字号",Action::FontSize,0,false,24);
        f.button( {
          24,557,136,58
        },"快刷",Action::FastTest,0,true);
        f.button( {
          172,557,136,58
        },"清残影",Action::CleanTest);
        f.button( {
          320,557,136,58
        },"全刷",Action::FullTest);
        wrap(&f,"全刷需主动点击。阅读停顿不自动闪屏；多次输入后的停笔可清残影。",24,647,432,22,0,4);
        break;
      }
      case View::Tools:
      header(f,"更多工具","保留扩展位置，按需要逐个加入");
      appButton(f,"calendar",{24,186,432,88},false,24);
      appButton(f,"messages",{24,294,432,88},false,24);
      appButton(f,"ai",{24,402,432,88},false,24);
      wrap(&f,"每个 App 只提交内容与交互意图。文件保存、输入法和屏幕刷新由公共模块处理。",24,536,432,24,0,5);
      f.button( {
        24,684,432,56
      },"导出笔记与待办",Action::Export,0,false,22);
      break;
    }
    if(!notice_.empty()) {
      f.rect( {
        0,770,480,30
      },true,false);
      f.text(14,774,notice_.c_str(),16);
    }
  }
}
