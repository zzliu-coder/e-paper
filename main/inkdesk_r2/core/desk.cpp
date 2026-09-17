#include "desk.h"
#include <cstdio>
namespace inkdesk {
  namespace {
    constexpr AppEntry registry[]= {
      {
        "notes","记事本",Action::Notes,true
      }, {
        "reader","电子书",Action::Reader,true
      },
      {
        "todo","待办事项",Action::Todo,true
      }, {
        "display","显示测试",Action::Lab,true
      },
      {
        "tools","更多工具",Action::Tools,true
      }, {
        "settings","设备设置",Action::Settings,true
      },
      {
        "calendar","日历",Action::Reserved,false
      }, {
        "messages","消息",Action::Reserved,false
      },
      {
        "ai","AI 终端",Action::Reserved,false
      }
    };
  }
  const AppEntry*appRegistry(size_t&n) {
    n=sizeof(registry)/sizeof(registry[0]);
    return registry;
  }
  void Desk::changeView(View v) {
    view_=v;
    readerRequested_=settingsRequested_=false;
    listPage_=0;
    ++epoch_;
    ime_.clear();
    notice_.clear();
    requestIntent(Intent::Page);
  }
  void Desk::dataChanged(uint32_t now) {
    if(!dirty())firstDirty_=now;
    ++dataRevision_;
    lastEdit_=now;
  }
  bool Desk::append(const char*s) {
    if(view_==View::Editor&&selectedNote_<docs_.noteCount)return docs_.notes[selectedNote_].body.append(s);
    if(view_==View::TodoEditor)return todoDraft_.append(s);
    return false;
  }
  bool Desk::erase() {
    if(view_==View::Editor&&selectedNote_<docs_.noteCount)return docs_.notes[selectedNote_].body.pop();
    if(view_==View::TodoEditor)return todoDraft_.pop();
    return false;
  }
  bool Desk::apply(const Event&e,uint32_t now) {
    if(e.action==Action::None)return false;
    if(e.epoch!=epoch_)return false;
    bool textEdited=false,changed=true;
    switch(e.action) {
      case Action::Home:if(view_==View::TodoEditor&&!todoDraft_.empty()) {
        notice_.assign("先保存待办，再返回首页");
        break;
      }if(editing()&&!ime_.empty()) {
        notice_.assign("先选字，或用清空键取消拼音");
        break;
      }saveRequested_=true;
      changeView(View::Home);
      break;
      case Action::Notes:changeView(View::Notes);
      break;
      case Action::Todo:changeView(View::Todo);
      break;
      case Action::Lab:changeView(View::Lab);
      break;
      case Action::Tools:changeView(View::Tools);
      break;
      case Action::Reader:saveRequested_=true;
      changeView(View::Reader);
      readerRequested_=true;
      break;
      case Action::Settings:saveRequested_=true;
      readerRequested_=false;
      settingsRequested_=true;
      ++epoch_;
      notice_.assign("正在打开原有设备设置");
      break;
      case Action::NewNote:
      if(docs_.noteCount==kNoteLimit) {
        notice_.assign("笔记已满：本版最多 6 篇");
        break;
      }
      selectedNote_=docs_.noteCount++;
      docs_.notes[selectedNote_].body.clear();
      dataChanged(now);
      changeView(View::Editor);
      break;
      case Action::OpenNote:if(e.value<0||size_t(e.value)>=docs_.noteCount)return false;
      selectedNote_=size_t(e.value);
      changeView(View::Editor);
      break;
      case Action::NewTodo:if(docs_.todoCount==kTodoLimit) {
        notice_.assign("待办已满：本版最多 16 项");
        break;
      }todoDraft_.clear();
      changeView(View::TodoEditor);
      break;
      case Action::ToggleTodo:if(e.value<0||size_t(e.value)>=docs_.todoCount)return false;
      docs_.todos[size_t(e.value)].done=!docs_.todos[size_t(e.value)].done;
      dataChanged(now);
      break;
      case Action::Save:
      if(editing()&&!ime_.empty()) {
        notice_.assign("先选择候选字，再保存");
        break;
      }
      if(view_==View::TodoEditor) {
        if(todoDraft_.empty()) {
          notice_.assign("先输入一条待办");
          break;
        }if(docs_.todoCount>=kTodoLimit)return false;
        auto&t=docs_.todos[docs_.todoCount++];
        t.text=todoDraft_;
        t.done=false;
        dataChanged(now);
        changeView(View::Todo);
      }
      saveRequested_=true;
      notice_.assign(dirty()?"正在保存":"已保存");
      break;
      case Action::Back:
      if(editing()&&!ime_.empty()) {
        notice_.assign("先选字，或用清空键取消拼音");
        break;
      }
      if(view_==View::TodoEditor&&!todoDraft_.empty()) {
        notice_.assign("点击保存完成待办；清空后可返回");
        break;
      }
      saveRequested_=true;
      changeView(view_==View::Editor?View::Notes:view_==View::TodoEditor?View::Todo:View::Home);
      break;
      case Action::Letter:
      if(!editing()||e.value<32||e.value>126)return false;
      notice_.clear();
      if(chinese_&&e.value>='a'&&e.value<='z') {
        if(!ime_.letter(char(e.value)))notice_.assign("拼音已满，请先选字");
      }
      else {
        char s[2]= {
          char(e.value),0
        };
        if(!ime_.empty()) {
          notice_.assign("请先完成当前拼音");
          break;
        }textEdited=append(s);
        if(!textEdited)notice_.assign("文字已满，未删除已有内容");
      }break;
      case Action::Backspace:if(!editing())return false;
      notice_.clear();
      if(!ime_.empty())ime_.backspace();
      else textEdited=erase();
      break;
      case Action::Space:
      if(!editing())return false;
      if(chinese_&&!ime_.empty()) {
        Text<48> word;
        if(!ime_.candidate(0,word)) {
          notice_.assign("词表未收录：可逐字输入");
          break;
        }if(append(word.c_str())) {
          ime_.clear();
          textEdited=true;
        }else notice_.assign("文字已满");
      }
      else {
        textEdited=append(" ");
        if(!textEdited)notice_.assign("文字已满");
      }break;
      case Action::Enter:if(!editing())return false;
      if(!ime_.empty()) {
        notice_.assign("请先选字");
        break;
      }textEdited=append("\n");
      if(!textEdited)notice_.assign("文字已满");
      break;
      case Action::ToggleLanguage:if(!editing())return false;
      if(!ime_.empty()) {
        notice_.assign("先选字，或清空拼音");
        break;
      }chinese_=!chinese_;
      break;
      case Action::Candidate: {
        if(!editing()||e.composition!=ime_.revision())return false;
        Text<48> word;
        if(e.value<0||!ime_.candidate(size_t(e.value),word))return false;
        if(append(word.c_str())) {
          ime_.clear();
          textEdited=true;
          notice_.clear();
        }else notice_.assign("文字已满");
        break;
      }
      case Action::CandidateNext:if(!editing()||e.composition!=ime_.revision())return false;
      ime_.nextPage();
      break;
      case Action::InputCancel:if(!editing())return false;
      if(!ime_.empty())ime_.clear();
      else if(view_==View::TodoEditor)todoDraft_.clear();
      break;
      case Action::FastTest:requestIntent(Intent::Probe);
      break;
      case Action::CleanTest:requestIntent(Intent::Clean);
      break;
      case Action::FullTest:requestIntent(Intent::Full);
      break;
      case Action::Pattern:pattern_=(pattern_+1)%3;
      requestIntent(Intent::Page);
      break;
      case Action::FontSize:fontSize_=fontSize_==24?28:fontSize_==28?32:24;
      requestIntent(Intent::Page);
      break;
      case Action::Previous:if(listPage_)--listPage_;
      break;
      case Action::Next: {
        size_t total=view_==View::Todo?docs_.todoCount:docs_.noteCount;
        if((listPage_+1)*5<total)++listPage_;
        break;
      }
      case Action::Export:saveRequested_=true;
      exportRequested_=true;
      notice_.assign("导出普通 Markdown 文件");
      break;
      case Action::Reserved:notice_.assign("扩展入口：尚未接入服务");
      break;
      default:return false;
    }
    if(textEdited&&view_==View::Editor)dataChanged(now);
    if (changed) ++revision_;
    return changed;
  }
  void Desk::saved(uint32_t rev,bool ok) {
    // Serial revision ordering, with the same half-range contract as the clock.
    // An old completion cannot roll back a newer committed snapshot. Unknown
    // future acknowledgments are never accepted as evidence of a real write.
    const auto newer=[](uint32_t a,uint32_t b) {
      return a!=b && uint32_t(a-b)<0x80000000u;
    };
    if (newer(rev,dataRevision_) || newer(savedRevision_,rev)) return;
    if (ok && rev==savedRevision_) return; // duplicate, already acknowledged
    if (rev==dataRevision_) saveRequested_=false;
    if (ok) {
      savedRevision_=rev;
      notice_.assign(dirty()?"已有更新，继续保存中":"已保存");
      ++revision_;
    } else {
      message("保存失败：内容仍在内存，请勿关机");
    }
  }
  namespace {
    struct Codec {
      uint8_t*out=nullptr;
      const uint8_t*in=nullptr;
      size_t capacity=0,pos=0;
      bool ok=true;
      void put(uint8_t v) {
        if(pos>=capacity) {
          ok=false;
          return;
        }out[pos++]=v;
      }
      uint8_t get() {
        if(pos>=capacity) {
          ok=false;
          return 0;
        }return in[pos++];
      }
      void text(const char*s,size_t n) {
        put(uint8_t(n));
        put(uint8_t(n>>8));
        for(size_t i=0;i<n;++i)put(uint8_t(s[i]));
      }
      template<size_t N>void text(Text<N>*dest) {
        size_t n=get();
        n|=size_t(get())<<8;
        if(!ok||n>N||n>capacity-pos||!validUtf8(reinterpret_cast<const char*>(in+pos),n)) {
          ok=false;
          return;
        }if(dest)dest->assign(reinterpret_cast<const char*>(in+pos),n);
        pos+=n;
      }
    };
    bool decodePass(const uint8_t*data,size_t size,Documents*dest) {
      Codec c {
        nullptr,data,size,0,true
      };
      if(c.get()!=1)return false;
      size_t nn=c.get(),nt=c.get();
      if(nn>kNoteLimit||nt>kTodoLimit)return false;
      for(size_t i=0;i<nn&&c.ok;++i)c.text<kNoteBytes>(dest?&dest->notes[i].body:nullptr);
      for(size_t i=0;i<nt&&c.ok;++i) {
        uint8_t done=c.get();
        if(done>1)return false;
        c.text<kTodoBytes>(dest?&dest->todos[i].text:nullptr);
        if(dest)dest->todos[i].done=done!=0;
      }
      if(!c.ok||c.pos!=size)return false;
      if(dest) {
        dest->noteCount=nn;
        dest->todoCount=nt;
      }return true;
    }
  }
  bool Desk::encode(uint8_t*out,size_t cap,size_t&size)const {
    if(!out) {
      size=0;
      return false;
    }Codec c {
      out,nullptr,cap,0,true
    };
    c.put(1);
    c.put(uint8_t(docs_.noteCount));
    c.put(uint8_t(docs_.todoCount));
    for(size_t i=0;i<docs_.noteCount;++i)c.text(docs_.notes[i].body.c_str(),docs_.notes[i].body.size());
    for(size_t i=0;i<docs_.todoCount;++i) {
      c.put(docs_.todos[i].done?1:0);
      c.text(docs_.todos[i].text.c_str(),docs_.todos[i].text.size());
    }size=c.ok?c.pos:0;
    return c.ok;
  }
  bool Desk::decode(const uint8_t*data,size_t size) {
    if(!data||!decodePass(data,size,nullptr))return false;
    if(!decodePass(data,size,&docs_))return false;
    dataRevision_=savedRevision_=0;
    saveRequested_=false;
    changeView(View::Home);
    ++revision_;
    return true;
  }
  bool Engine::enqueue(Event e) {
    bool ok=events_.push(e);
    if(!ok)desk_.message("输入队列已满，请稍慢一点");
    return ok;
  }
  bool Engine::tap(int x,int y) {
    if(!haveVisible_)return false;
    Event e=visible_.hit(x,y);
    if(e.action==Action::None)return false;
    // Alphabet/backspace geometry is stable. Candidate/navigation geometry can be stale.
    bool stable=e.action==Action::Letter||e.action==Action::Backspace;
    if(refresh_.busy()&&!stable) {
      ++blocked_;
      return false;
    }
    if(e.epoch!=desk_.epoch()) {
      ++stale_;
      return false;
    }
    if((e.action==Action::Candidate||e.action==Action::CandidateNext)&&e.composition!=desk_.composition()) {
      ++stale_;
      return false;
    }
    return enqueue(e);
  }
  bool Engine::key(char c) {
    Event e {
      c=='\b'?Action::Backspace:c=='\n'?Action::Enter:c==' '?Action::Space:Action::Letter,int(static_cast<unsigned char>(c)),desk_.epoch(),desk_.composition()
    };
    return enqueue(e);
  }
  void Engine::pump(uint32_t now) {
    Event event;
    while (events_.pop(event)) {
      if (event.epoch != desk_.epoch()) {
        ++stale_;
        continue;
      }
      if (desk_.apply(event, now)) lastInput_ = now;
    }
    if (desk_.revision() != observedRevision_) {
      // Also notices/errors raised outside the event queue, e.g. SD completion.
      refresh_.request(desk_.takeIntent(), fullRect(), desk_.revision(), now);
      observedRevision_=desk_.revision();
    }
    // Only an editor may request idle cleanup, and only after sufficient fast
    // refreshes. Ordinary reading pauses never trigger an extra flash.
    if (desk_.editing() && !*desk_.pinyin() && refresh_.cleanDue() &&
        !refresh_.busy() && !refresh_.pending() && !refresh_.fault() &&
        elapsed(now, lastInput_) >= refresh_.config().cleanIdleMs) {
      refresh_.request(Intent::Clean, fullRect(), desk_.revision(), now);
    }
  }
  bool Engine::prepare(uint32_t now) {
    // Never redraw or reuse an in-flight snapshot. The driver may still read it.
    if (!refresh_.ready(now)) return false;
    desk_.draw(inflight_);
    if (haveVisible_ && inflight_.sameDrawing(visible_) && refresh_.discardUnchanged()) {
      // The pixels are already right; publish only the newer hit/composition
      // metadata. No extra framebuffer, allocation, or physical refresh.
      visible_=inflight_;
      return false;
    }
    if (!refresh_.start(now,desk_.editing(),job_)) return false;
    if (inflight_.overflow) {
      refresh_.complete(job_.sequence,now,false);
      return false;
    }
    return true;
  }
  bool Engine::complete(uint32_t now,bool success) {
    if(!refresh_.complete(job_.sequence,now,success))return false;
    visible_=inflight_;
    haveVisible_=true;
    return true;
  }
}
