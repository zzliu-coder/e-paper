#pragma once
#include "types.h"
#include "ime.h"
#include "refresh.h"
namespace inkdesk {
  constexpr size_t kNoteLimit=6,kNoteBytes=1536,kTodoLimit=16,kTodoBytes=128;
  struct Note {
    Text<kNoteBytes> body;
  };
  struct Todo {
    Text<kTodoBytes>text;
    bool done=false;
  };
  struct Documents {
    std::array<Note,kNoteLimit>notes {};
    std::array<Todo,kTodoLimit>todos {};
    size_t noteCount=0,todoCount=0;
  };
  struct AppEntry {
    const char*id;
    const char*title;
    Action action;
    bool available;
  };
  const AppEntry*appRegistry(size_t&count);
  class Desk {
    Documents docs_ {};
    View view_=View::Home;
    Pinyin ime_;
    Text<kTodoBytes>todoDraft_;
    Text<96>notice_;
    uint32_t revision_=1,epoch_=1,dataRevision_=0,savedRevision_=0,firstDirty_=0,lastEdit_=0;
    size_t selectedNote_=0,listPage_=0;
    int fontSize_=24,pattern_=0;
    bool chinese_=true,saveRequested_=false;
    bool readerRequested_=false,settingsRequested_=false,exportRequested_=false;
    Intent requestedIntent_=Intent::Page;
    void requestIntent(Intent intent) {
      requestedIntent_ = std::max(requestedIntent_, intent);
    }
    void changeView(View v);
    bool append(const char*s);
    bool erase();
    void dataChanged(uint32_t now);
    public:
    bool apply(const Event&e,uint32_t now);
    void draw(Frame&f)const;
    const Documents&documents()const {
      return docs_;
    }
    View view()const {
      return view_;
    }uint32_t revision()const {
      return revision_;
    }uint32_t epoch()const {
      return epoch_;
    }
    uint32_t composition()const {
      return ime_.revision();
    }const char*pinyin()const {
      return ime_.raw();
    }
    const char*notice()const {
      return notice_.c_str();
    }
    bool editing()const {
      return view_==View::Editor||view_==View::TodoEditor;
    }
    bool dirty()const {
      return dataRevision_!=savedRevision_;
    }uint32_t dataRevision()const {
      return dataRevision_;
    }
    bool saveDue(uint32_t now)const {
      return dirty()&&(saveRequested_||elapsed(now,lastEdit_)>=1500||elapsed(now,firstDirty_)>=10000);
    }
    void saved(uint32_t snapshotRevision,bool ok);
    bool encode(uint8_t*out,size_t capacity,size_t&size)const;
    bool decode(const uint8_t*data,size_t size);
    Intent takeIntent() {
      auto i=requestedIntent_;
      requestedIntent_=Intent::Interactive;
      return i;
    }
    bool takeReader() {
      bool b=readerRequested_;
      readerRequested_=false;
      return b;
    }
    bool takeSettings() {
      bool b=settingsRequested_;
      settingsRequested_=false;
      return b;
    }
    bool takeExport() {
      bool b=exportRequested_;
      exportRequested_=false;
      return b;
    }
    void message(const char*text) {
      if (text && std::strcmp(notice_.c_str(),text)!=0 && notice_.assign(text)) ++revision_;
    }
  };
  class Engine {
    Desk desk_;
    RefreshScheduler refresh_;
    Queue<Event,96>events_;
    Frame visible_ {},inflight_ {};
    RefreshJob job_ {};
    uint32_t blocked_=0,stale_=0,lastInput_=0,observedRevision_=0;
    bool haveVisible_=false;
    public:
    explicit Engine(RefreshConfig cfg= {}):refresh_(cfg) {}
    Desk&desk() {
      return desk_;
    }const Desk&desk()const {
      return desk_;
    }
    RefreshScheduler&refresh() {
      return refresh_;
    }const RefreshScheduler&refresh()const {
      return refresh_;
    }
    const Frame&visible()const {
      return visible_;
    }const Frame&inflight()const {
      return inflight_;
    }
    const RefreshJob&job()const {
      return job_;
    }bool haveVisible()const {
      return haveVisible_;
    }
    void boot(uint32_t now) {
      // The external reader/settings may have replaced every pixel. Until our
      // new frame completes, old hit targets must not accept touches.
      haveVisible_=false;
      Event discarded;
      while (events_.pop(discarded)) ++stale_;
      observedRevision_=desk_.revision();
      refresh_.request(std::max(Intent::Clean,desk_.takeIntent()),fullRect(),desk_.revision(),now);
    }
    bool enqueue(Event e);
    bool tap(int x,int y);
    bool key(char c);
    void pump(uint32_t now);
    bool prepare(uint32_t now);
    bool complete(uint32_t now,bool success);
    uint32_t rejected()const {
      return events_.rejected();
    }uint32_t blocked()const {
      return blocked_;
    }uint32_t stale()const {
      return stale_;
    }
  };
}
