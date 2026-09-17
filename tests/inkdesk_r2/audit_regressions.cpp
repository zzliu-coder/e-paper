// R2 adversarial regressions. These tests also compile against the unmodified
// R1 core so the audit findings can be reproduced, rather than asserted.
#include "desk.h"
#include "journal.h"

#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace inkdesk;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
struct Runner {
  int passed = 0, failed = 0;
  void test(const char* name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) {
      ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n';
    }
  }
};
void apply(Desk& desk, Action action, int value = 0) {
  CHECK(desk.apply({action, value, desk.epoch(), desk.composition()}, 50));
}
void post(Engine& engine, Action action, int value = 0) {
  const auto& desk = engine.desk();
  CHECK(engine.enqueue({action, value, desk.epoch(), desk.composition()}));
}
void boot(Engine& engine) {
  engine.boot(0);
  CHECK(engine.prepare(0));
  CHECK(engine.complete(1100, true));
}
struct MemoryStore final : FileStore {
  std::map<std::string, std::vector<uint8_t>> files;
  IoStatus read(const char* path, uint8_t* out, size_t cap, size_t& n) override {
    n = 0;
    const auto it = files.find(path);
    if (it == files.end()) return IoStatus::Missing;
    if (it->second.size() > cap) return IoStatus::TooLarge;
    n = it->second.size();
    if (n) std::memcpy(out, it->second.data(), n);
    return IoStatus::Ok;
  }
  bool write(const char* path, const uint8_t* data, size_t n) override {
    files[path].assign(data, data + n);
    return true;
  }
};
void put(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
}
void makeJournal(MemoryStore& store) {
  Journal journal(store);
  uint8_t out[8]{}; size_t n = 0;
  CHECK(journal.load(out, sizeof(out), n) == IoStatus::Missing);
  const uint8_t a[] = {1, 2, 3}, b[] = {4, 5, 6};
  CHECK(journal.save(a, sizeof(a)));
  CHECK(journal.save(b, sizeof(b)));
}
}
int main() {
  Runner r;
  r.test("no-op navigation does not submit an identical frame", [] {
    Engine engine; boot(engine);
    post(engine, Action::Previous); engine.pump(1200);
    CHECK(!engine.prepare(1300));
    CHECK(!engine.refresh().pending());
    CHECK(engine.visible().revision == engine.desk().revision());
  });
  r.test("manual FULL survives a later page request in the same event batch", [] {
    Engine engine; boot(engine);
    post(engine, Action::FullTest); post(engine, Action::Pattern);
    engine.pump(1200); CHECK(engine.prepare(1300));
    CHECK(engine.job().mode == RefreshMode::Full);
  });
  r.test("external model notice schedules its own repaint", [] {
    Engine engine; boot(engine);
    engine.desk().message("新的存储状态");
    engine.pump(1200); CHECK(engine.prepare(1300));
  });
  r.test("reentering after an external page invalidates old hit targets", [] {
    Engine engine; boot(engine);
    engine.boot(1300);
    CHECK(!engine.haveVisible());
    CHECK(!engine.tap(100, 240));
  });
  r.test("button text has a bounded rectangle inside its button", [] {
    Frame frame;
    const Rect button{24, 478, 82, 48};
    frame.button(button, "中华人民共和国", Action::Candidate, 0, false, 20);
    const auto box = frame.draws[1].box;
    CHECK(box.w > 0 && box.h > 0);
    CHECK(box.x >= button.x && box.y >= button.y);
    CHECK(box.x + box.w <= button.x + button.w);
    CHECK(box.y + box.h <= button.y + button.h);
  });
  r.test("newer journal wrapper cannot be overwritten by older firmware", [] {
    MemoryStore store; makeJournal(store);
    auto& newer = store.files["/inkdesk/state.b"];
    put(newer, 4, 2); put(newer, 20, crc32(newer.data(), 20));
    const auto preserved = newer;
    Journal journal(store); uint8_t out[8]{}; size_t n = 0;
    CHECK(journal.load(out, sizeof(out), n) == IoStatus::Ok);
    CHECK(out[0] == 1 && n == 3);
    CHECK(journal.writesBlocked());
    CHECK(!journal.save(out, n));
    CHECK(store.files["/inkdesk/state.b"] == preserved);
  });
  r.test("oversized unseen slot blocks writes even with a valid older slot", [] {
    MemoryStore store; makeJournal(store);
    store.files["/inkdesk/state.b"].resize(kMaxPayload + 25, 0);
    Journal journal(store); uint8_t out[8]{}; size_t n = 0;
    CHECK(journal.load(out, sizeof(out), n) == IoStatus::Ok);
    CHECK(journal.writesBlocked());
    CHECK(!journal.save(out, n));
  });
  r.test("late old save acknowledgment cannot roll committed revision backwards", [] {
    Desk desk; apply(desk, Action::NewNote);
    const uint32_t old = desk.dataRevision();
    apply(desk, Action::ToggleLanguage); apply(desk, Action::Letter, 'a');
    desk.saved(desk.dataRevision(), true); CHECK(!desk.dirty());
    const auto uiRevision = desk.revision();
    desk.saved(old, true);
    CHECK(!desk.dirty()); CHECK(desk.revision() == uiRevision);
  });
  r.test("unknown future save acknowledgment cannot mutate document state", [] {
    Desk desk; apply(desk, Action::NewNote);
    const auto revision = desk.revision();
    desk.saved(desk.dataRevision() + 1, true);
    CHECK(desk.revision() == revision); CHECK(desk.dirty());
  });
  r.test("last pending frame drains even when there is no more user input", [] {
    Engine engine; boot(engine);
    post(engine, Action::NewNote); engine.pump(1200);
    CHECK(engine.prepare(1300)); CHECK(engine.complete(1700, true));
    CHECK(engine.key('n')); engine.pump(1800); CHECK(engine.prepare(1900));
    const auto inflightRevision = engine.inflight().revision;
    CHECK(engine.key('i')); engine.pump(2000);
    CHECK(engine.inflight().revision == inflightRevision);
    CHECK(engine.complete(2300, true)); CHECK(engine.prepare(2300));
    CHECK(engine.complete(2700, true));
    CHECK(engine.visible().revision == engine.desk().revision());
  });
  r.test("failed display completion never publishes the requested new page", [] {
    Engine engine; boot(engine); const auto revision = engine.visible().revision;
    post(engine, Action::Notes); engine.pump(1200); CHECK(engine.prepare(1300));
    CHECK(!engine.complete(1700, false)); CHECK(engine.refresh().fault());
    CHECK(engine.visible().revision == revision);
  });
  r.test("empty journal record is valid with a zero-capacity null output", [] {
    MemoryStore store; Journal writer(store); uint8_t out[1]{}; size_t n = 0;
    CHECK(writer.load(out, sizeof(out), n) == IoStatus::Missing);
    CHECK(writer.save(nullptr, 0));
    Journal reader(store);
    CHECK(reader.load(nullptr, 0, n) == IoStatus::Ok); CHECK(n == 0);
  });
  r.test("explicit fast probe stays fast even when periodic cleaning is due", [] {
    RefreshConfig config; config.cleanEvery=1;
    RefreshScheduler scheduler(config); RefreshJob job;
    scheduler.request(Intent::Page,fullRect(),1,0);
    CHECK(scheduler.start(100,false,job)); CHECK(scheduler.complete(job.sequence,500,true));
    CHECK(scheduler.cleanDue());
    scheduler.request(Intent::Probe,fullRect(),2,600);
    CHECK(scheduler.start(700,false,job)); CHECK(job.mode==RefreshMode::Fast);
  });
  r.test("100 no-op interactions add no physical frame submissions", [] {
    Engine engine; boot(engine); const auto initial=engine.refresh().stats().submitted;
    for(uint32_t i=0;i<100;++i) {
      const auto now=1200+i*200;
      post(engine,Action::Previous); engine.pump(now);
      CHECK(!engine.prepare(now+100));
    }
    CHECK(engine.refresh().stats().submitted==initial);
    CHECK(engine.refresh().stats().unchanged==100);
  });
  r.test("all three manual display modes submit even when pixels are identical", [] {
    Engine engine; boot(engine); uint32_t now=1200;
    for(const auto action:{Action::FastTest,Action::CleanTest,Action::FullTest}) {
      post(engine,action); engine.pump(now); CHECK(engine.prepare(now+100));
      const auto expected=action==Action::FullTest?RefreshMode::Full:
          action==Action::CleanTest?RefreshMode::Clean:RefreshMode::Fast;
      CHECK(engine.job().mode==expected); CHECK(engine.complete(now+4000,true));
      now+=5000;
    }
    CHECK(engine.refresh().stats().submitted==4);
  });
  r.test("changed pixels are never mistaken for an unchanged drawing", [] {
    Frame a,b; a.text(24,100,"你好"); b=a; CHECK(a.sameDrawing(b));
    b.revision++; b.epoch++; b.composition++; CHECK(a.sameDrawing(b));
    b.draws[0].text.assign("您好"); CHECK(!a.sameDrawing(b));
    b=a; b.draws[0].box.w--; CHECK(!a.sameDrawing(b));
    b=a; b.draws[0].black=false; CHECK(!a.sameDrawing(b));
    b=a; b.overflow=true; CHECK(!a.sameDrawing(b));
  });
  r.test("skipping an offscreen model change does not lose later visible content", [] {
    Engine engine; boot(engine);
    post(engine,Action::Pattern); engine.pump(1200); CHECK(!engine.prepare(1300));
    post(engine,Action::Lab); engine.pump(1400); CHECK(engine.prepare(1500));
    CHECK(engine.complete(2000,true)); CHECK(engine.visible().view==View::Lab);
    size_t blackSquares=0;
    const auto& frame=engine.visible();
    for(size_t i=0;i<frame.drawCount;++i) {
      const auto& d=frame.draws[i];
      if(d.kind==DrawKind::Fill&&d.box.w==42&&d.box.h==42)++blackSquares;
    }
    CHECK(blackSquares==30);
  });
  r.test("reentry clears queued events belonging to a no-longer-visible page", [] {
    Engine engine; boot(engine); post(engine,Action::NewNote);
    engine.boot(1200); engine.pump(1201);
    CHECK(engine.desk().documents().noteCount==0);
  });
  r.test("unknown journal magic retains the valid older slot read-only", [] {
    MemoryStore store; makeJournal(store); store.files["/inkdesk/state.b"][3]='2';
    Journal journal(store); uint8_t out[8]{}; size_t n=0;
    CHECK(journal.load(out,sizeof(out),n)==IoStatus::Ok);
    CHECK(journal.writesBlocked()); CHECK(out[0]==1);
  });
  r.test("ordinary truncated slot remains recoverable and writable", [] {
    MemoryStore store; makeJournal(store); store.files["/inkdesk/state.b"].resize(7);
    Journal journal(store); uint8_t out[8]{}; size_t n=0;
    CHECK(journal.load(out,sizeof(out),n)==IoStatus::Ok);
    CHECK(!journal.writesBlocked()); CHECK(journal.save(out,n));
  });
  r.test("both unsupported slots remain untouched", [] {
    MemoryStore store; makeJournal(store);
    for(auto& pair:store.files) {put(pair.second,4,2); put(pair.second,20,crc32(pair.second.data(),20));}
    const auto before=store.files;
    Journal journal(store); uint8_t out[8]{}; size_t n=0;
    CHECK(journal.load(out,sizeof(out),n)==IoStatus::Unsupported);
    CHECK(journal.writesBlocked()); CHECK(!journal.save(out,0)); CHECK(store.files==before);
  });
  r.test("a cancelled external request cannot survive a Home transition", [] {
    Desk desk; apply(desk,Action::Reader); apply(desk,Action::Home); CHECK(!desk.takeReader());
    apply(desk,Action::Settings); apply(desk,Action::Home); CHECK(!desk.takeSettings());
  });
  r.test("queue overflow notice is observable even without an accepted action", [] {
    Engine engine; boot(engine);
    for(int i=0;i<96;++i) CHECK(engine.enqueue({Action::None,0,engine.desk().epoch(),0}));
    CHECK(!engine.enqueue({Action::None,0,engine.desk().epoch(),0}));
    engine.pump(1200); CHECK(engine.prepare(1300));
    CHECK(engine.rejected()==1);
  });
  r.test("repeating the same out-of-band notice is idempotent", [] {
    Desk desk; desk.message("相同消息"); const auto revision=desk.revision();
    desk.message("相同消息"); CHECK(desk.revision()==revision);
  });
  std::cout << "RESULT " << r.passed << " passed, " << r.failed << " failed\n";
  return r.failed ? 1 : 0;
}
