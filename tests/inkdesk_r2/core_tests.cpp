#include "desk.h"
#include "journal.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <random>
using namespace inkdesk;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" @"+std::to_string(__LINE__));}while(false)
struct Runner{int passed=0,failed=0;void run(const char*name,const std::function<void()>&fn){try{fn();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}};
static Event event(Desk&d,Action a,int v=0){return{a,v,d.epoch(),d.composition()};}
static void act(Desk&d,Action a,int v=0,uint32_t now=100){CHECK(d.apply(event(d,a,v),now));}
static void letters(Desk&d,const char*s){while(*s)act(d,Action::Letter,*s++);}
struct MemoryStore:FileStore{
 std::map<std::string,std::vector<uint8_t>>files;int truncate=-1;bool offline=false,lie=false;std::string unreadable;
 IoStatus read(const char*p,uint8_t*out,size_t cap,size_t&n)override{n=0;if(offline||unreadable==p)return IoStatus::Unavailable;auto i=files.find(p);if(i==files.end())return IoStatus::Missing;if(i->second.size()>cap)return IoStatus::TooLarge;n=i->second.size();std::copy(i->second.begin(),i->second.end(),out);return IoStatus::Ok;}
 bool write(const char*p,const uint8_t*d,size_t n)override{if(offline)return false;bool cut=truncate>=0&&size_t(truncate)<n;size_t size=cut?size_t(truncate):n;files[p]=std::vector<uint8_t>(d,d+size);return !cut||lie;}
};
int main(){Runner r;
 r.run("utf8 accepts ASCII/CJK/emoji",[]{CHECK(validUtf8("hello中文😀",15));});
 r.run("utf8 rejects invalid scalar encodings",[]{const std::vector<std::string>bad={"\xC0\x80","\xED\xA0\x80","\xF4\x90\x80\x80","\x80","\xE4\xB8",std::string("a\0b",3)};for(auto&s:bad)CHECK(!validUtf8(s.data(),s.size()));});
 r.run("text refuses overflow atomically",[]{Text<6>t;CHECK(t.assign("你好"));CHECK(!t.append("a"));CHECK(!t.assign("1234567"));CHECK(std::string(t.c_str())=="你好");});
 r.run("UTF8 backspace removes one scalar",[]{Text<20>t;CHECK(t.assign("a中😀"));CHECK(t.pop());CHECK(std::string(t.c_str())=="a中");CHECK(t.pop());CHECK(std::string(t.c_str())=="a");CHECK(t.pop());CHECK(!t.pop());});
 r.run("rectangle union clips panel edges",[]{auto q=unite({-10,-10,20,20},{470,790,100,100});CHECK(q.x==0&&q.y==0&&q.w==480&&q.h==800);});
 r.run("bounded queue preserves FIFO on overflow",[]{Queue<int,3>q;CHECK(q.push(1));CHECK(q.push(2));CHECK(q.push(3));CHECK(!q.push(4));int x=0;for(int i=1;i<=3;++i){CHECK(q.pop(x));CHECK(x==i);}CHECK(!q.pop(x));CHECK(q.rejected()==1);});
 r.run("refresh coalesces bursts",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Interactive,{0,0,10,10},1,0);s.request(Intent::Interactive,{20,20,10,10},2,20);CHECK(!s.start(40,false,j));CHECK(s.start(56,false,j));CHECK(j.revision==2&&j.requested.w==30);CHECK(s.stats().submitted==1);});
 r.run("refresh max wait prevents starvation",[]{RefreshScheduler s;RefreshJob j;for(uint32_t t=0;t<=100;t+=10)s.request(Intent::Interactive,fullRect(),t,t);CHECK(s.start(100,false,j));});
 r.run("strong refresh intent never downgraded",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Full,fullRect(),1,0);s.request(Intent::Interactive,{1,1,1,1},2,1);CHECK(s.start(1,false,j));CHECK(j.mode==RefreshMode::Full);});
 r.run("backend without window uses full address region",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,{10,20,30,40},1,0);CHECK(s.start(100,false,j));CHECK(j.applied.w==480&&j.applied.h==800);CHECK(j.mode==RefreshMode::Fast);});
 r.run("window capable backend preserves requested region",[]{RefreshConfig c;c.hasWindow=true;RefreshScheduler s(c);RefreshJob j;s.request(Intent::Page,{10,20,30,40},1,0);CHECK(s.start(100,false,j));CHECK(j.applied.w==30);});
 r.run("only one frame may be in flight",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,false,j));s.request(Intent::Page,fullRect(),2,200);CHECK(!s.start(300,false,j));});
 r.run("newer state remains pending behind in-flight frame",[]{RefreshScheduler s;RefreshJob a,b;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,false,a));s.request(Intent::Page,fullRect(),3,200);CHECK(s.complete(a.sequence,500,true));CHECK(s.start(500,false,b));CHECK(b.revision==3);});
 r.run("stale completion cannot unlock driver",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,false,j));CHECK(!s.complete(j.sequence+1,500,true));CHECK(s.busy());});
 r.run("periodic clean is folded into next page",[]{RefreshConfig c;c.cleanEvery=2;RefreshScheduler s(c);RefreshJob j;for(uint32_t n=0;n<2;++n){s.request(Intent::Page,fullRect(),n,n*1000);CHECK(s.start(n*1000+100,false,j));CHECK(s.complete(j.sequence,n*1000+500,true));}CHECK(s.cleanDue());CHECK(!s.ready(99999));s.request(Intent::Page,fullRect(),3,100000);CHECK(s.start(100100,false,j));CHECK(j.mode==RefreshMode::Clean);});
 r.run("automatic clean deferred while typing",[]{RefreshConfig c;c.cleanEvery=1;RefreshScheduler s(c);RefreshJob j;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,true,j));s.complete(j.sequence,500,true);s.request(Intent::Page,fullRect(),2,600);CHECK(s.start(700,true,j));CHECK(j.mode==RefreshMode::Fast);});
 r.run("wraparound clock preserves debounce",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,fullRect(),1,0xfffffff0u);CHECK(!s.start(4,false,j));CHECK(s.start(40,false,j));});
 r.run("timeout latches without pretending BUSY ended",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,false,j));CHECK(s.timedOut(7200));CHECK(s.busy()&&s.fault());s.recover(false,2,8000);CHECK(s.fault());s.recover(true,2,8000);CHECK(s.start(8000,false,j));CHECK(j.mode==RefreshMode::Full);});
 r.run("failed refresh requires independent recovery",[]{RefreshScheduler s;RefreshJob j;s.request(Intent::Page,fullRect(),1,0);CHECK(s.start(100,false,j));CHECK(!s.complete(j.sequence,500,false));s.request(Intent::Page,fullRect(),2,600);CHECK(!s.start(1000,false,j));CHECK(s.stats().failures==1);});
 r.run("empty region never causes refresh",[]{RefreshScheduler s;s.request(Intent::Page,{},1,0);CHECK(!s.pending());});
 r.run("basic pinyin phrase works offline",[]{Pinyin p;for(char c:std::string("nihao"))CHECK(p.letter(c));Text<48>w;CHECK(p.candidate(0,w));CHECK(std::string(w.c_str())=="你好");});
 r.run("candidate pagination preserves deterministic order",[]{Pinyin p;p.letter('s');p.letter('h');p.letter('i');CHECK(p.count()>4);Text<48>w;p.nextPage();CHECK(p.candidate(0,w));CHECK(std::string(w.c_str())=="使");});
 r.run("IME invalid letter does not mutate composition",[]{Pinyin p;auto v=p.revision();CHECK(!p.letter('1'));CHECK(v==p.revision()&&p.empty());});
 r.run("unknown pinyin exposes no fake candidates",[]{Pinyin p;for(char c:std::string("zzz"))p.letter(c);Text<48>w;CHECK(p.count()==0&&!p.candidate(0,w));});
 r.run("IME length is bounded",[]{Pinyin p;for(int i=0;i<32;++i)CHECK(p.letter('a'));CHECK(!p.letter('a'));});
 r.run("note create edit Chinese and persist revision",[]{Desk d;act(d,Action::NewNote);letters(d,"nihao");act(d,Action::Candidate,0);CHECK(std::string(d.documents().notes[0].body.c_str())=="你好");CHECK(d.dirty());});
 r.run("composition does not silently disappear on back",[]{Desk d;act(d,Action::NewNote);letters(d,"ni");act(d,Action::Back);CHECK(d.view()==View::Editor&&std::string(d.pinyin())=="ni");});
 r.run("uncommitted pinyin is not accidentally saved as English",[]{Desk d;act(d,Action::NewNote);letters(d,"ni");act(d,Action::Save);CHECK(d.documents().notes[0].body.empty());CHECK(std::string(d.pinyin())=="ni");});
 r.run("stale candidate event rejected",[]{Desk d;act(d,Action::NewNote);letters(d,"ni");auto old=event(d,Action::Candidate);letters(d,"hao");CHECK(!d.apply(old,100));CHECK(d.documents().notes[0].body.empty());});
 r.run("old screen event rejected after navigation",[]{Desk d;auto old=event(d,Action::NewNote);act(d,Action::Todo);CHECK(!d.apply(old,100));CHECK(d.documents().noteCount==0);});
 r.run("note byte capacity does not truncate existing text",[]{Desk d;act(d,Action::NewNote);act(d,Action::ToggleLanguage);for(size_t i=0;i<kNoteBytes+10;++i)act(d,Action::Letter,'x');CHECK(d.documents().notes[0].body.size()==kNoteBytes);});
 r.run("note count limit visible",[]{Desk d;for(size_t i=0;i<kNoteLimit;++i)act(d,Action::NewNote);act(d,Action::NewNote);CHECK(d.documents().noteCount==kNoteLimit);});
 r.run("todo create and toggle",[]{Desk d;act(d,Action::NewTodo);letters(d,"xuexi");act(d,Action::Candidate,0);act(d,Action::Save);CHECK(d.documents().todoCount==1);CHECK(std::string(d.documents().todos[0].text.c_str())=="学习");act(d,Action::ToggleTodo,0);CHECK(d.documents().todos[0].done);});
 r.run("unsaved todo draft protected by back",[]{Desk d;act(d,Action::NewTodo);act(d,Action::ToggleLanguage);letters(d,"task");act(d,Action::Back);CHECK(d.view()==View::TodoEditor);});
 r.run("old save acknowledgment cannot clear newer edits",[]{Desk d;act(d,Action::NewNote);auto rev=d.dataRevision();act(d,Action::ToggleLanguage);letters(d,"a");d.saved(rev,true);CHECK(d.dirty());d.saved(d.dataRevision(),true);CHECK(!d.dirty());});
 r.run("save failure preserves dirty document",[]{Desk d;act(d,Action::NewNote);d.saved(d.dataRevision(),false);CHECK(d.dirty());CHECK(std::string(d.notice()).find("失败")!=std::string::npos);});
 r.run("codec roundtrip preserves CJK/todos",[]{Desk d;act(d,Action::NewNote);letters(d,"nihao");act(d,Action::Candidate,0);act(d,Action::NewTodo);letters(d,"ceshi");act(d,Action::Candidate,0);act(d,Action::Save);std::array<uint8_t,kMaxPayload>b{};size_t n=0;CHECK(d.encode(b.data(),b.size(),n));Desk copy;CHECK(copy.decode(b.data(),n));CHECK(std::string(copy.documents().notes[0].body.c_str())=="你好");CHECK(copy.documents().todoCount==1);});
 r.run("codec validation does not partially replace data",[]{Desk d;act(d,Action::NewNote);std::array<uint8_t,kMaxPayload>b{};size_t n=0;CHECK(d.encode(b.data(),b.size(),n));b[n++]=0xFF;CHECK(!d.decode(b.data(),n));CHECK(d.documents().noteCount==1);});
 r.run("codec rejects unknown version and excessive counts",[]{Desk d;uint8_t b[]={2,0,0};CHECK(!d.decode(b,sizeof b));b[0]=1;b[1]=200;CHECK(!d.decode(b,sizeof b));});
 r.run("CRC32 matches standard known vector",[]{const char*s="123456789";CHECK(crc32(reinterpret_cast<const uint8_t*>(s),9)==0xCBF43926u);});
 r.run("journal must be loaded before overwrite",[]{MemoryStore m;Journal j(m);uint8_t x=1;CHECK(!j.save(&x,1));CHECK(m.files.empty());});
 r.run("journal read-write roundtrip",[]{MemoryStore m;Journal j(m);uint8_t out[20];size_t n=0;CHECK(j.load(out,sizeof out,n)==IoStatus::Missing);uint8_t data[]={1,2,3};CHECK(j.save(data,3));Journal k(m);CHECK(k.load(out,sizeof out,n)==IoStatus::Ok);CHECK(n==3&&out[2]==3);});
 r.run("interrupted inactive slot write preserves old state",[]{MemoryStore m;Journal j(m);uint8_t out[20],a=7,b=9;size_t n=0;j.load(out,sizeof out,n);CHECK(j.save(&a,1));m.truncate=12;CHECK(!j.save(&b,1));Journal k(m);CHECK(k.load(out,sizeof out,n)==IoStatus::Ok&&out[0]==7);});
 r.run("lying storage write rejected by readback",[]{MemoryStore m;Journal j(m);uint8_t out[20],a=7;size_t n=0;j.load(out,sizeof out,n);m.truncate=24;m.lie=true;CHECK(!j.save(&a,1));});
 r.run("corrupt latest slot falls back to previous",[]{MemoryStore m;Journal j(m);uint8_t out[20],a=7,b=9;size_t n=0;j.load(out,sizeof out,n);CHECK(j.save(&a,1));CHECK(j.save(&b,1));m.files["/inkdesk/state.b"].back()^=1;Journal k(m);CHECK(k.load(out,sizeof out,n)==IoStatus::Ok&&out[0]==7);});
 r.run("corrupt both slots never auto-overwrites",[]{MemoryStore m;m.files["/inkdesk/state.a"]={1,2};m.files["/inkdesk/state.b"]={3};Journal j(m);uint8_t out[20],a=7;size_t n=0;CHECK(j.load(out,sizeof out,n)==IoStatus::Corrupt);CHECK(!j.save(&a,1));CHECK(m.files["/inkdesk/state.a"].size()==2);});
 r.run("unavailable storage distinct from empty storage",[]{MemoryStore m;m.offline=true;Journal j(m);uint8_t out[20];size_t n=0;CHECK(j.load(out,sizeof out,n)==IoStatus::Unavailable);});
 r.run("all interrupted write positions keep recoverable old state",[]{for(int cut=0;cut<40;++cut){MemoryStore m;Journal j(m);uint8_t out[100];size_t n=0;j.load(out,sizeof out,n);uint8_t a=7,b[40]{};CHECK(j.save(&a,1));m.truncate=cut;CHECK(!j.save(b,sizeof b));Journal k(m);CHECK(k.load(out,sizeof out,n)==IoStatus::Ok&&n==1&&out[0]==7);}});
 r.run("view drawing stays within fixed command capacity",[]{Desk d;for(Action a:{Action::Home,Action::Notes,Action::Todo,Action::Reader,Action::Lab,Action::Tools,Action::NewNote}){act(d,a);Frame f;d.draw(f);CHECK(!f.overflow);for(size_t i=0;i<f.hitCount;++i){auto b=f.hits[i].box;CHECK(b.x>=0&&b.y>=0&&b.x+b.w<=480&&b.y+b.h<=800);}}});
 r.run("test patterns do not exceed frame capacity",[]{Desk d;act(d,Action::Lab);for(int i=0;i<3;++i){Frame f;d.draw(f);CHECK(!f.overflow);act(d,Action::Pattern);}});
 r.run("engine coalesces keyboard burst into final state",[]{Engine e;e.boot(0);CHECK(e.prepare(0));CHECK(e.complete(1100,true));e.enqueue(event(e.desk(),Action::NewNote));e.pump(1101);CHECK(e.prepare(1201));CHECK(e.complete(1601,true));for(char c:std::string("nihao"))CHECK(e.key(c));e.pump(1602);CHECK(std::string(e.desk().pinyin())=="nihao");CHECK(e.prepare(1702));});
 r.run("frame snapshot immutable while keyboard changes model",[]{Engine e;e.boot(0);e.prepare(0);e.complete(1100,true);e.enqueue(event(e.desk(),Action::NewNote));e.pump(1200);CHECK(e.prepare(1300));auto old=e.inflight().revision;auto first=e.inflight().drawCount;for(char c:std::string("nihao"))e.key(c);e.pump(1310);CHECK(e.inflight().revision==old&&e.inflight().drawCount==first);CHECK(e.desk().revision()>old);});
 r.run("busy navigation suppressed rather than misrouted",[]{Engine e;e.boot(0);e.prepare(0);e.complete(1100,true);e.enqueue(event(e.desk(),Action::Lab));e.pump(1200);e.prepare(1300);CHECK(!e.tap(30,200));CHECK(e.blocked()==1);});
 r.run("late queued actions do not activate next screen",[]{Engine e;e.boot(0);e.prepare(0);e.complete(1100,true);auto old=event(e.desk(),Action::NewNote);e.enqueue(event(e.desk(),Action::Todo));e.enqueue(old);e.pump(1300);CHECK(e.desk().documents().noteCount==0);CHECK(e.stale()==1);});
 r.run("100000 randomized UTF8 edits retain valid bounded data",[]{std::mt19937 rng(42);Text<96>t;const char*parts[]={"a","中","😀","é","\n"};for(int i=0;i<100000;++i){if(rng()%3==0)t.pop();else t.append(parts[rng()%5]);CHECK(t.size()<=96);CHECK(validUtf8(t.c_str(),t.size()));}});
 r.run("10000 randomized UI events remain bounded",[]{std::mt19937 rng(9);Desk d;Frame f;for(int i=0;i<10000;++i){Action a=Action(rng()%(int(Action::Reserved)+1));d.apply(event(d,a,int(rng()%130)),uint32_t(i));d.draw(f);CHECK(!f.overflow);CHECK(d.documents().noteCount<=kNoteLimit&&d.documents().todoCount<=kTodoLimit);}});
 r.run("rectangle arithmetic resists extreme coordinates",[]{auto q=clip({INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX});CHECK(q.empty());Rect b{INT32_MIN,0,2,4};CHECK(!b.contains(INT32_MAX,1));});
 r.run("timed-out frame cannot be published by late completion",[]{Engine e;e.boot(0);CHECK(e.prepare(0));CHECK(e.refresh().timedOut(8000));CHECK(!e.complete(8100,true));CHECK(!e.haveVisible()&&e.refresh().busy());});
 r.run("unknown journal payload can block later writes",[]{MemoryStore m;Journal j(m);uint8_t out[20],x=2;size_t n=0;j.load(out,sizeof out,n);CHECK(j.save(&x,1));j.blockWrites();CHECK(!j.save(&x,1));});
 r.run("unreadable second slot keeps recovered data read-only",[]{MemoryStore m;Journal j(m);uint8_t out[20],x=7;size_t n=0;j.load(out,sizeof out,n);CHECK(j.save(&x,1));m.unreadable="/inkdesk/state.b";Journal k(m);CHECK(k.load(out,sizeof out,n)==IoStatus::Ok);CHECK(out[0]==7&&k.writesBlocked());CHECK(!k.save(&x,1));});
 r.run("Home preserves unsaved todo draft",[]{Desk d;act(d,Action::NewTodo);act(d,Action::ToggleLanguage);letters(d,"task");act(d,Action::Home);CHECK(d.view()==View::TodoEditor);});
 r.run("null codec destination is rejected",[]{Desk d;size_t n=1;CHECK(!d.encode(nullptr,20,n)&&n==0);});
 r.run("idle editor cleans only after accumulated fast updates",[]{RefreshConfig c;c.cleanEvery=1;Engine e(c);e.boot(0);e.prepare(0);e.complete(1100,true);e.enqueue(event(e.desk(),Action::NewNote));e.pump(1200);e.prepare(1300);e.complete(1700,true);e.pump(2000);CHECK(!e.refresh().pending());e.pump(3000);CHECK(e.prepare(3000));CHECK(e.job().mode==RefreshMode::Clean);e.complete(4100,true);e.pump(8000);CHECK(!e.refresh().pending());});
 r.run("reading pause never borrows editor idle cleanup",[]{RefreshConfig c;c.cleanEvery=1;Engine e(c);e.boot(0);e.prepare(0);e.complete(1100,true);e.enqueue(event(e.desk(),Action::Reader));e.pump(1200);e.prepare(1300);e.complete(1700,true);CHECK(e.refresh().cleanDue());e.pump(100000);CHECK(!e.refresh().pending());});
 std::cout<<"MEMORY Engine="<<sizeof(Engine)<<" Frame="<<sizeof(Frame)<<" Desk="<<sizeof(Desk)<<" Journal="<<sizeof(Journal)<<" bytes\n";
 std::cout<<"RESULT "<<r.passed<<" passed, "<<r.failed<<" failed\n";return r.failed?1:0;
}
