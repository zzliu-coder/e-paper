#include "paper/ime.hpp"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <sstream>
namespace paper {
    namespace {
        uint16_t u16(const uint8_t*p) {
            return p[0]|uint16_t(p[1])<<8;
        }
        uint32_t u32(const uint8_t*p) {
            return u16(p)|(uint32_t(u16(p+2))<<16);
        }
        std::string learnKey(const Candidate&c) {
            return c.pinyin+"\t"+c.text;
        }
    }
    Status PinyinDictionary::normalize(std::string&s) {
        std::string out;
        for(size_t i=0;i<s.size();++i) {
            unsigned char c=s[i];
            if(c==0xc3&&i+1<s.size()&&uint8_t(s[i+1])==0xbc) {
                out+='v';
                ++i;
            }
            else if(c==' '||c=='\'') {
            }
            else if(c>='A'&&c<='Z')out+=char(c+32);
            else if(c>='a'&&c<='z')out+=char(c);
            else return Status::fail(Error::Invalid,"拼音只能包含字母、空格和分隔符");
        }
        if(out.empty()||out.size()>47)return Status::fail(Error::Invalid,"拼音长度无效");
        s=out;
        return {
        };
    }
    std::string PinyinDictionary::nineKey(const std::string&s) {
        static const char*map="22233344455566677778889999";
        std::string out;
        for(char c:s)if(c>='a'&&c<='z')out+=map[c-'a'];
        return out;
    }
    Status PinyinDictionary::query(const std::string&input,bool nine,std::vector<Candidate>&out,size_t limit) {
        out.clear();
        if(input.empty())return {
        };
        if(input.size()>47||!limit||limit>64)return Status::fail(Error::Invalid,"候选查询超出范围");
        std::string query=input;
        if(nine) {
            if(query.find_first_not_of("23456789")!=query.npos)return Status::fail(Error::Invalid,"九键输入无效");
        }
        else {
            auto q=normalize(query);
            if(!q)return q;
        }
        LeaseGuard lease;
        auto st=lease.acquire(store_.gate(),"sd","ime");
        if(!st)return st;
        std::string path;
        st=store_.path("paper/ime/"+std::string(nine?"nine":"pinyin")+".pim",path,false);
        if(!st)return Status::fail(Error::ResourceMissing,"中文词典未安装");
        FILE*f=fopen(path.c_str(),"rb");
        if(!f)return Status::fail(Error::Io,"无法读取中文词典");
        uint8_t header[32];
        if(fread(header,1,32,f)!=32||memcmp(header,"PIM1",4)||(u32(header+4)&0x7fffffffu)!=1||((u32(header+4)&0x80000000u)&&!allowTest_)||u32(header+12)!=64||u32(header+28)!=crc32(header,28)) {
            fclose(f);
            return Status::fail(Error::Corrupt,"中文词典头无效");
        }
        uint32_t n=u32(header+8),data=u32(header+16),length=u32(header+20);
        if(!n||n>500000||data!=32+n*64||length>64*1024*1024||fseek(f,0,SEEK_END)!=0||ftell(f)!=long(uint64_t(data)+length)) {
            fclose(f);
            return Status::fail(Error::Corrupt,"中文词典索引无效");
        }
        auto row=[&](uint32_t i,uint8_t*b) {
            return i<n&&fseek(f,32+long(i)*64,SEEK_SET)==0&&fread(b,1,64,f)==64&&memchr(b,0,48);
        };
        uint32_t lo=0,hi=n;
        uint8_t r[64];
        while(lo<hi) {
            uint32_t m=lo+(hi-lo)/2;
            if(!row(m,r)) {
                fclose(f);
                return Status::fail(Error::Corrupt,"词典索引读取失败");
            }
            if(std::string((char*)r)<query)lo=m+1;
            else hi=m;
        }
        for(uint32_t i=lo;i<n&&i-lo<2048;++i) {
            if(!row(i,r)) {
                fclose(f);
                return Status::fail(Error::Corrupt,"词典索引读取失败");
            }
            std::string key((char*)r);
            if(key.compare(0,query.size(),query))break;
            uint32_t off=u32(r+48);
            uint16_t wl=u16(r+52),pl=u16(r+54);
            uint32_t freq=u32(r+56);
            if(wl==0||wl>192||pl>47||uint64_t(off)+wl+pl>length) {
                fclose(f);
                return Status::fail(Error::Corrupt,"词典条目越界");
            }
            char buf[240] {
            };
            if(fseek(f,data+off,SEEK_SET)||fread(buf,1,wl+pl,f)!=size_t(wl+pl)) {
                fclose(f);
                return Status::fail(Error::Io,"词典条目读取失败");
            }
            Candidate c {
                std::string(buf,wl),std::string(buf+wl,pl),std::min<uint32_t>(freq,1000000)
            };
            if(!validUtf8(c.text)) {
                fclose(f);
                return Status::fail(Error::Corrupt,"词典字符无效");
            }
            c.score+=key==query?2000000:0;
            auto learned=learned_.find(learnKey(c));
            if(learned!=learned_.end())c.score+=std::min<uint32_t>(learned->second,100)*10000;
            auto same=std::find_if(out.begin(),out.end(),[&](auto&x){return x.text==c.text;});
            if(same==out.end())out.push_back(c);
            else if(c.score>same->score)*same=c;
            std::stable_sort(out.begin(),out.end(),[](auto&a,auto&b){if(a.score!=b.score)return a.score>b.score;if(a.text.size()!=b.text.size())return a.text.size()<b.text.size();return a.text<b.text;});
            if(out.size()>limit)out.resize(limit);
        }
        fclose(f);
        return {
        };
    }
    Status PinyinDictionary::loadLearning() {
        std::string payload;
        auto st=store_.loadRecord("ime-learning",payload);
        if(st.code==Error::NotFound)return {
        };
        if(!st)return st;
        std::map<std::string,uint32_t>m;
        std::istringstream in(payload);
        std::string line;
        while(std::getline(in,line)) {
            auto pos=line.rfind('\t');
            if(pos==line.npos)continue;
            char*e=nullptr;
            unsigned long n=strtoul(line.c_str()+pos+1,&e,10);
            if(!e||*e||n>100)continue;
            auto k=line.substr(0,pos);
            if(k.size()>240||!validUtf8(k)||m.size()>=512)continue;
            m[k]=uint32_t(n);
        }
        learned_=std::move(m);
        return {
        };
    }
    Status PinyinDictionary::learn(const std::vector<Candidate>&list) {
        auto next=learned_;
        for(auto&c:list) {
            if(c.text.find_first_of("\t\r\n")!=c.text.npos||c.pinyin.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=c.pinyin.npos)continue;
            auto key=learnKey(c);
            if(next.size()>=512&&!next.count(key)) {
                auto oldest=std::min_element(next.begin(),next.end(),[](auto&a,auto&b){return a.second<b.second;});
                next.erase(oldest);
            }
            next[key]=std::min<uint32_t>(100,next[key]+1);
        }
        std::string text;
        for(auto&x:next)text+=x.first+"\t"+std::to_string(x.second)+"\n";
        auto st=store_.saveRecord("ime-learning",text);
        if(st)learned_=std::move(next);
        return st;
    }
    void TextSession::wipe(std::string&s) {
        volatile char*p=s.empty()?nullptr:&s[0];
        for(size_t i=0;i<s.size();++i)p[i]=0;
        s.clear();
    }
    TextSession::~TextSession() {
        cancel();
    }
    Status TextSession::begin(const std::string&t,size_t max,bool secret) {
        if(active_)return Status::fail(Error::Busy,"已有输入会话");
        if(!validUtf8(t)||!max||max>8192||t.size()>max)return Status::fail(Error::Invalid,"初始文本或长度无效");
        draft_=original_=t;
        cursor_=t.size();
        maxBytes_=max;
        secret_=secret;
        mode_=secret?InputMode::English:InputMode::Pinyin9;
        active_=true;
        choices_.clear();
        preedit_.clear();
        learnPending_.clear();
        ++generation_;
        return {
        };
    }
    Status TextSession::refresh() {
        ++generation_;
        return dict_.query(preedit_,mode_==InputMode::Pinyin9,choices_);
    }
    Status TextSession::key(char c) {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(mode_==InputMode::Pinyin9||mode_==InputMode::Pinyin26) {
            if((mode_==InputMode::Pinyin9&&(c<'2'||c>'9'))||(mode_==InputMode::Pinyin26&&!(c>='a'&&c<='z')))return Status::fail(Error::Invalid,"按键不属于当前模式");
            if(preedit_.size()>=47)return Status::fail(Error::TooLarge,"请先选择候选词");
            preedit_+=c;
            auto st=refresh();
            if(!st) {
                preedit_.pop_back();
                refresh();
            }
            return st;
        }
        return literal(std::string(1,c));
    }
    Status TextSession::literal(const std::string&s) {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(!preedit_.empty())return Status::fail(Error::Busy,"请先选词或清除拼音");
        if(!validUtf8(s)||s.find('\0')!=s.npos||s.size()>maxBytes_-draft_.size())return Status::fail(Error::TooLarge,"文本无效或长度超限");
        draft_.insert(cursor_,s);
        cursor_+=s.size();
        ++generation_;
        return {
        };
    }
    Status TextSession::backspace() {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(!preedit_.empty()) {
            preedit_.pop_back();
            return refresh();
        }
        size_t prev=previousGrapheme(draft_,cursor_);
        if(prev<cursor_)draft_.erase(prev,cursor_-prev);
        cursor_=prev;
        ++generation_;
        return {
        };
    }
    Status TextSession::move(int direction) {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(!preedit_.empty())return Status::fail(Error::Busy,"先完成当前拼音");
        cursor_=direction<0?previousGrapheme(draft_,cursor_):nextGrapheme(draft_,cursor_);
        ++generation_;
        return {
        };
    }
    Status TextSession::mode(InputMode m) {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(int(m)<0||int(m)>4)return Status::fail(Error::Invalid,"输入模式无效");
        if(secret_&&(m==InputMode::Pinyin9||m==InputMode::Pinyin26))return Status::fail(Error::Unsupported,"密码使用英文、数字或符号");
        if(!preedit_.empty()&&m!=mode_)return Status::fail(Error::Busy,"请先选词或退格清除拼音");
        mode_=m;
        ++generation_;
        return {
        };
    }
    Status TextSession::choose(size_t i,uint64_t generation) {
        if(!active_||generation!=generation_)return Status::fail(Error::Conflict,"候选已经更新，请重新选择");
        if(i>=choices_.size())return Status::fail(Error::Invalid,"候选不存在");
        Candidate c=choices_[i];
        if(c.text.size()>maxBytes_-draft_.size())return Status::fail(Error::TooLarge,"字段长度不够");
        draft_.insert(cursor_,c.text);
        cursor_+=c.text.size();
        if(!secret_&&learnPending_.size()<256)learnPending_.push_back(c);
        preedit_.clear();
        choices_.clear();
        ++generation_;
        return {
        };
    }
    Status TextSession::confirm(std::string&out) {
        if(!active_)return Status::fail(Error::Conflict,"没有输入会话");
        if(!preedit_.empty())return Status::fail(Error::Busy,"请先选词，拼音尚未上屏");
        out=draft_;
        if(!secret_&&!learnPending_.empty())dict_.learn(learnPending_);
        cancel();
        return {
        };
    }
    void TextSession::cancel() {
        wipe(original_);
        wipe(draft_);
        wipe(preedit_);
        cursor_=0;
        choices_.clear();
        learnPending_.clear();
        active_=false;
        secret_=false;
        ++generation_;
    }
    std::string TextSession::visible()const {
        if(!secret_)return draft_;
        std::vector<Rune>r;
        decodeUtf8(draft_,r);
        return std::string(r.size(),'*');
    }
}
