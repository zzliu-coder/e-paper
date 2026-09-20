#include "paper/document.hpp"
#include "CssParser.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
namespace paper {
    namespace {
        uint16_t u16(const uint8_t*p) {
            return p[0]|uint16_t(p[1])<<8;
        }
        uint32_t u32(const uint8_t*p) {
            return u16(p)|(uint32_t(u16(p+2))<<16);
        }
        std::string lower(std::string s) {
            for(char&c:s)if(c>='A'&&c<='Z')c=char(c+32);
            return s;
        }
        std::string local(std::string s) {
            auto n=s.find(':');
            if(n!=s.npos)s=s.substr(n+1);
            return lower(s);
        }
        std::string entity(const std::string&s) {
            std::string o;
            for(size_t i=0;i<s.size();++i) {
                if(s[i]!='&') {
                    o+=s[i];
                    continue;
                }
                auto end=s.find(';',i+1);
                if(end==s.npos||end-i>16) {
                    o+='&';
                    continue;
                }
                auto k=s.substr(i+1,end-i-1);
                std::string v;
                if(k=="amp")v="&";
                else if(k=="lt")v="<";
                else if(k=="gt")v=">";
                else if(k=="quot")v="\"";
                else if(k=="apos")v="'";
                else if(k=="nbsp")v=" ";
                else if(k=="mdash")v="—";
                else if(k=="ndash")v="–";
                else if(!k.empty()&&k[0]=='#') {
                    char*e=nullptr;
                    bool hex=k.size()>2&&(k[1]=='x'||k[1]=='X');
                    auto n=strtoul(k.c_str()+(hex?2:1),&e,hex?16:10);
                    if(e&&!*e&&n>0&&n<=0x10ffff&&!(n>=0xd800&&n<=0xdfff))v=encodeUtf8(uint32_t(n));
                }
                if(v.empty())o+=s.substr(i,end-i+1);
                else o+=v;
                i=end;
            }
            return o;
        }
        struct Token {
            bool tag=false,close=false,self=false;
            std::string name,text;
            std::map<std::string,std::string>attr;
        };
        Status tokenize(const std::string&s,const std::function<Status(const Token&)>&fn,DocumentProgress progress={}) {
            size_t pos=0;
            while(pos<s.size()) {
                if(progress){auto st=progress("metadata",pos,s.size());if(!st)return st;}
                if(s[pos]!='<') {
                    auto e=s.find('<',pos);
                    if(e==s.npos)e=s.size();
                    auto st=fn({false,false,false,"",entity(s.substr(pos,e-pos)),{}});
                    if(!st)return st;
                    pos=e;
                    continue;
                }
                if(s.compare(pos,4,"<!--")==0) {
                    auto e=s.find("-->",pos+4);
                    if(e==s.npos)return Status::fail(Error::Corrupt,"注释未结束");
                    pos=e+3;
                    continue;
                }
                if(s.compare(pos,9,"<![CDATA[")==0) {
                    auto e=s.find("]]>",pos+9);
                    if(e==s.npos)return Status::fail(Error::Corrupt,"CDATA未结束");
                    auto st=fn({false,false,false,"",s.substr(pos+9,e-pos-9),{}});
                    if(!st)return st;
                    pos=e+3;
                    continue;
                }
                size_t e=pos+1;
                char quote=0;
                int bracket=0;
                for(;e<s.size();++e) {
                    char c=s[e];
                    if(quote) {
                        if(c==quote)quote=0;
                    }
                    else if(c=='\''||c=='"')quote=c;
                    else if(c=='[')++bracket;
                    else if(c==']'&&bracket)--bracket;
                    else if(c=='>'&&!bracket)break;
                }
                if(e==s.size()||e-pos>65536)return Status::fail(Error::Corrupt,"标记不完整或过长");
                auto raw=s.substr(pos+1,e-pos-1);
                pos=e+1;
                if(raw.empty()||raw[0]=='?'||raw[0]=='!')continue;
                Token t;
                t.tag=true;
                size_t i=0;
                if(raw[i]=='/') {
                    t.close=true;
                    ++i;
                }
                while(i<raw.size()&&isspace(uint8_t(raw[i])))++i;
                size_t b=i;
                while(i<raw.size()&&!isspace(uint8_t(raw[i]))&&raw[i]!='/')++i;
                t.name=local(raw.substr(b,i-b));
                while(i<raw.size()) {
                    while(i<raw.size()&&isspace(uint8_t(raw[i])))++i;
                    if(i>=raw.size())break;
                    if(raw[i]=='/') {
                        t.self=true;
                        ++i;
                        continue;
                    }
                    b=i;
                    while(i<raw.size()&&!isspace(uint8_t(raw[i]))&&raw[i]!='=')++i;
                    std::string key=local(raw.substr(b,i-b));
                    while(i<raw.size()&&isspace(uint8_t(raw[i])))++i;
                    std::string val;
                    if(i<raw.size()&&raw[i]=='=') {
                        ++i;
                        while(i<raw.size()&&isspace(uint8_t(raw[i])))++i;
                        if(i<raw.size()&&(raw[i]=='"'||raw[i]=='\'')) {
                            char q=raw[i++];
                            b=i;
                            while(i<raw.size()&&raw[i]!=q)++i;
                            val=raw.substr(b,i-b);
                            if(i<raw.size())++i;
                        }
                        else {
                            b=i;
                            while(i<raw.size()&&!isspace(uint8_t(raw[i])))++i;
                            val=raw.substr(b,i-b);
                        }
                    }
                    if(key.empty()||t.attr.size()>64)return Status::fail(Error::Corrupt,"属性无效");
                    t.attr[key]=entity(val);
                }
                auto st=fn(t);
                if(!st)return st;
            }
            return {
            };
        }
        std::string attr(const Token&t,const std::string&k) {
            auto i=t.attr.find(k);
            return i==t.attr.end()?"":i->second;
        }
        Status utf16(const std::vector<uint8_t>&v,bool be,std::string&text) {
            text.clear();
            size_t i=2;
            while(i<v.size()) {
                if(i+1>=v.size())return Status::fail(Error::Corrupt,"UTF-16末尾不完整");
                uint32_t c=be?(uint32_t(v[i])<<8|v[i+1]):u16(v.data()+i);
                i+=2;
                if(c>=0xd800&&c<=0xdbff) {
                    if(i+1>=v.size())return Status::fail(Error::Corrupt,"UTF-16代理对不完整");
                    uint32_t d=be?(uint32_t(v[i])<<8|v[i+1]):u16(v.data()+i);
                    if(d<0xdc00||d>0xdfff)return Status::fail(Error::Corrupt,"UTF-16代理对无效");
                    i+=2;
                    c=0x10000+((c-0xd800)<<10)+d-0xdc00;
                }
                else if(c>=0xdc00&&c<=0xdfff)return Status::fail(Error::Corrupt,"UTF-16代理对无效");
                text+=encodeUtf8(c);
            }
            return {
            };
        }
    }
    Status archivePath(const std::string&base,const std::string&href,std::string&out,std::string*fragment) {
        out.clear();
        auto hash=href.find('#');
        std::string p=href.substr(0,hash);
        if(fragment)*fragment=hash==href.npos?"":href.substr(hash+1);
        if(p.find(':')!=p.npos||p.find('\\')!=p.npos||(!p.empty()&&p[0]=='/'))return Status::fail(Error::Unsupported,"只支持书内链接");
        std::string decoded;
        for(size_t i=0;i<p.size();++i) {
            if(p[i]=='%'&&i+2<p.size()) {
                char h[3]= {
                    p[i+1],p[i+2],0
                };
                char*end=nullptr;
                long n=strtol(h,&end,16);
                if(end&&!*end) {
                    if(n==0||n=='/'||n=='\\')return Status::fail(Error::Invalid,"链接路径编码无效");
                    decoded+=char(n);
                    i+=2;
                    continue;
                }
            }
            decoded+=p[i];
        }
        std::string full=base+decoded;
        std::vector<std::string>parts;
        std::istringstream in(full);
        std::string q;
        while(std::getline(in,q,'/')) {
            if(q.empty()||q==".")continue;
            if(q=="..") {
                if(parts.empty())return Status::fail(Error::Invalid,"链接越出书籍");
                parts.pop_back();
            }
            else parts.push_back(q);
        }
        for(auto&x:parts) {
            if(!out.empty())out+='/';
            out+=x;
        }
        if(out.empty())return Status::fail(Error::Invalid,"链接为空");
        return {
        };
    }
    Status markupToChapter(const std::string&html,const std::string&href,Chapter&out,size_t cap,const std::string&cssText,DocumentProgress progress) {
        out={};out.href=href;
        CssParser css("");
        if(cssText.size()>128*1024)return Status::fail(Error::TooLarge,"章节样式超过预算");
        HalFile cssFile(cssText);
        if(css.loadFromStream(cssFile)==CssParser::ParseResult::Error)return Status::fail(Error::Corrupt,"章节样式不可读");
        struct Frame {std::string tag;CssStyle style;bool heading=false;};
        std::vector<Frame> frames;
        std::vector<Link> active;
        int skip=0;bool head=false;
        auto newline=[&](){if(!out.text.empty()&&out.text.back()!='\n')out.text+='\n';};
        auto bounded=[&](){return out.text.size()>cap?Status::fail(Error::TooLarge,"章节文本超出预算"):Status{};};
        auto st=tokenize(html,[&](const Token&t)->Status {
            if(progress){auto s=progress("parse",out.text.size(),0);if(!s)return s;}
            if(!t.tag){
                if(skip||head)return {};
                const auto begin=out.text.size();
                for(char c:t.text){
                    if(c=='\r'||c=='\n'||c=='\t'||c==' '){if(!out.text.empty()&&out.text.back()!=' '&&out.text.back()!='\n')out.text+=' ';}
                    else out.text+=c;
                }
                if(!frames.empty()&&out.text.size()>begin){
                    const auto& f=frames.back();
                    int align=f.style.textAlign==CssTextAlign::Center?1:f.style.textAlign==CssTextAlign::Right?2:0;
                    bool underline=(uint8_t(f.style.textDecoration)&uint8_t(CssTextDecoration::Underline))!=0;
                    bool bold=f.style.fontWeight==CssFontWeight::Bold;
                    bool italic=f.style.fontStyle==CssFontStyle::Italic;
                    bool strike=(uint8_t(f.style.textDecoration)&uint8_t(CssTextDecoration::LineThrough))!=0;
                    const float indent=f.style.textIndent.toPixels(1,0);
                    if(align||f.heading||underline||bold||italic||strike||indent){
                        if(out.styles.size()>=4096)return Status::fail(Error::TooLarge,"章节样式段过多");
                        // Retain original UTF-8 text/anchor offsets. Styles stay separate.
                        out.styles.push_back({begin,out.text.size(),align,f.heading,underline,bold,italic,strike,
                            f.style.textIndent.unit==CssUnit::Em||f.style.textIndent.unit==CssUnit::Rem?std::max(-8.f,std::min(8.f,indent)):0.f});
                    }
                }
                return bounded();
            }
            if(t.name=="head"){head=!t.close;return {};}
            if(t.name=="script"||t.name=="style"){skip+=t.close?-1:1;skip=std::max(0,skip);return {};}
            if(skip||head)return {};
            const bool heading=t.name.size()==2&&t.name[0]=='h'&&t.name[1]>='1'&&t.name[1]<='6';
            if(t.name=="p"||t.name=="div"||t.name=="section"||t.name=="br"||t.name=="li"||t.name=="tr"||heading)newline();
            if(!t.close){
                auto id=attr(t,"id");if(id.empty())id=attr(t,"name");
                if(!id.empty()&&out.anchors.size()<2048)out.anchors.emplace(id,out.text.size());
                if(t.name=="a"&&!attr(t,"href").empty()){
                    if(active.size()>=16)return Status::fail(Error::TooLarge,"链接嵌套过深");
                    active.push_back({out.text.size(),out.text.size(),attr(t,"href")});
                }
                if(t.name=="img"){
                    newline();size_t begin=out.text.size();
                    out.text+="[图片";auto alt=attr(t,"alt");if(!alt.empty())out.text+="："+alt;out.text+="]\n";
                    std::string path;const auto slash=href.rfind('/');
                    auto valid=archivePath(slash==std::string::npos?"":href.substr(0,slash+1),attr(t,"src"),path);
                    if(valid&&!attr(t,"src").empty()){
                        if(out.images.size()>=256)return Status::fail(Error::TooLarge,"章节插图过多");
                        out.images.push_back({begin,out.text.size(),path});
                    }
                }
                if(!t.self&&t.name!="img"&&t.name!="br"&&t.name!="hr"&&t.name!="meta"&&t.name!="link"&&t.name!="input"){
                    if(frames.size()>=64)return Status::fail(Error::TooLarge,"章节嵌套过深");
                    CssStyle resolved;bool inheritedHeading=false;
                    if(!frames.empty()){resolved=frames.back().style;inheritedHeading=frames.back().heading;}
                    if(t.name=="b"||t.name=="strong"||heading)resolved.fontWeight=CssFontWeight::Bold;
                    if(t.name=="em"||t.name=="i")resolved.fontStyle=CssFontStyle::Italic;
                    if(t.name=="u")resolved.textDecoration=resolved.textDecoration|CssTextDecoration::Underline;
                    if(t.name=="s"||t.name=="del")resolved.textDecoration=resolved.textDecoration|CssTextDecoration::LineThrough;
                    // Semantic defaults precede the author's explicit CSS.
                    resolved.applyOver(css.resolveStyle(t.name,attr(t,"class")));
                    resolved.applyOver(CssParser::parseInlineStyle(attr(t,"style")));
                    frames.push_back({t.name,resolved,heading||inheritedHeading});
                }
            } else {
                if(t.name=="a"&&!active.empty()){auto l=active.back();active.pop_back();l.end=out.text.size();if(l.end>l.begin&&out.links.size()<2048)out.links.push_back(l);}
                for(size_t i=frames.size();i>0;--i)if(frames[i-1].tag==t.name){frames.resize(i-1);break;}
            }
            if(t.self&&t.name=="a"&&!active.empty())active.pop_back();
            return bounded();
        });
        if(!st)return st;
        if(!validUtf8(out.text))return Status::fail(Error::Corrupt,"章节不是有效UTF-8");
        return {};
    }
    Status LocalDocument::zipIndex() {
        FILE*f=fopen(absolute_.c_str(),"rb");
        if(!f)return Status::fail(Error::Io,"无法打开EPUB");
        if(fseek(f,0,SEEK_END)) {
            fclose(f);
            return Status::fail(Error::Io,"无法定位EPUB");
        }
        long sz=ftell(f);
        if(sz<22) {
            fclose(f);
            return Status::fail(Error::Corrupt,"EPUB文件不完整");
        }
        size_t tail=std::min<size_t>(65557,size_t(sz));
        std::vector<uint8_t>b(tail);
        fseek(f,sz-long(tail),SEEK_SET);
        bool ok=fread(b.data(),1,tail,f)==tail;
        size_t e=SIZE_MAX;
        if(ok)for(size_t i=tail-22;;--i) {
            if(u32(b.data()+i)==0x06054b50&&i+22+u16(b.data()+i+20)==tail) {
                e=i;
                break;
            }
            if(!i)break;
        }
        if(e==SIZE_MAX) {
            fclose(f);
            return Status::fail(Error::Corrupt,"缺少ZIP目录");
        }
        auto*h=b.data()+e;
        uint32_t n=u16(h+10),central=u32(h+16),centralSize=u32(h+12);
        if(u16(h+4)||u16(h+6)||u16(h+8)!=n||n==65535||n>4096||uint64_t(central)+centralSize>uint64_t(sz)) {
            fclose(f);
            return Status::fail(Error::Unsupported,"不支持多卷/ZIP64或目录过大的书籍");
        }
        if(fseek(f,central,SEEK_SET)) {
            fclose(f);
            return Status::fail(Error::Io,"目录定位失败");
        }
        size_t indexBytes=0;
        entries_.clear();
        for(uint32_t i=0;i<n;++i) {
            if(progress_){auto s=progress_("archive_index",i,n);if(!s){fclose(f);return s;}}
            uint8_t z[46];
            if(fread(z,1,46,f)!=46||u32(z)!=0x02014b50) {
                fclose(f);
                return Status::fail(Error::Corrupt,"ZIP目录损坏");
            }
            uint16_t name=u16(z+28),extra=u16(z+30),comment=u16(z+32);
            if(!name||name>512||uint64_t(ftell(f))+name+extra+comment>uint64_t(central)+centralSize) {
                fclose(f);
                return Status::fail(Error::Corrupt,"ZIP条目越界");
            }
            std::string path(name,'\0');
            if(fread(&path[0],1,name,f)!=name||fseek(f,extra+comment,SEEK_CUR)) {
                fclose(f);
                return Status::fail(Error::Io,"ZIP条目读取失败");
            }
            if(path.back()=='/')continue;
            std::string normalized;
            auto s=archivePath("",path,normalized);
            if(!s||normalized!=path) {
                fclose(f);
                return Status::fail(Error::Corrupt,"ZIP路径不规范");
            }
            indexBytes+=sizeof(Entry)+path.size();
            if(indexBytes>budget_.documentIndex) {
                fclose(f);
                return Status::fail(Error::TooLarge,"书籍目录超过索引预算");
            }
            entries_.push_back({path,u32(z+20),u32(z+24),u32(z+42),u32(z+16),u16(z+10),u16(z+8)});
        }
        fclose(f);
        std::sort(entries_.begin(),entries_.end(),[](auto&a,auto&b){return a.name<b.name;});
        for(size_t i=1;i<entries_.size();++i)if(entries_[i].name==entries_[i-1].name)return Status::fail(Error::Corrupt,"ZIP存在重复资源名");
        return {
        };
    }
    Status LocalDocument::item(const std::string&name,std::vector<uint8_t>&out,size_t cap) {
        out.clear();
        auto i=std::lower_bound(entries_.begin(),entries_.end(),name,[](auto&a,auto&b){return a.name<b;});
        if(i==entries_.end()||i->name!=name)return Status::fail(Error::NotFound,"书内资源不存在");
        auto&e=*i;
        if(e.flags&1)return Status::fail(Error::Unsupported,"不支持加密书籍");
        if(e.size>cap||e.compressed>cap+65536)return Status::fail(Error::TooLarge,"章节或资源超过预算");
        if(e.method!=0&&e.method!=8)return Status::fail(Error::Unsupported,"不支持此ZIP压缩方式");
        FILE*f=fopen(absolute_.c_str(),"rb");
        if(!f)return Status::fail(Error::Io,"读取EPUB失败");
        uint8_t h[30];
        bool ok=fseek(f,e.offset,SEEK_SET)==0&&fread(h,1,30,f)==30&&u32(h)==0x04034b50;
        if(!ok||u16(h+8)!=e.method||u16(h+6)!=e.flags||u16(h+26)>512) {
            fclose(f);
            return Status::fail(Error::Corrupt,"ZIP本地头无效");
        }
        std::string localName(u16(h+26),'\0');
        ok=fread(&localName[0],1,localName.size(),f)==localName.size()&&localName==e.name&&fseek(f,u16(h+28),SEEK_CUR)==0;
        std::vector<uint8_t>data(e.compressed);
        for(size_t at=0;ok&&at<data.size();){
            auto n=std::min<size_t>(32768,data.size()-at);
            ok=fread(data.data()+at,1,n,f)==n;at+=n;
            if(progress_){auto s=progress_("resource_read",at,data.size());if(!s){fclose(f);return s;}}
        }
        fclose(f);
        if(!ok)return Status::fail(Error::Corrupt,"ZIP压缩数据不完整");
        Status st;
        if(e.method==0) {
            if(e.compressed!=e.size)return Status::fail(Error::Corrupt,"ZIP长度不匹配");
            out=std::move(data);
        }
        else st=inflateRaw(data,out,e.size,cap,progress_);
        if(!st)return st;
        if(crc32(out.data(),out.size())!=e.crc) {
            out.clear();
            return Status::fail(Error::Corrupt,"书籍资源CRC错误");
        }
        return {
        };
    }
    void LocalDocument::close() {
        lease_.reset();
        opened_=false;
        // Retain one parsed book, but never a file handle or SD lease.
    }
    Status LocalDocument::open(const std::string&path,Metadata&out) {
        close();
        auto st=lease_.acquire(store_.gate(),"sd","reader");
        if(!st)return st;
        std::string absolute;
        st=store_.path(path,absolute,false);
        if(!st){close();return st;}
        if(cacheValid_&&relative_==path&&cachedGeneration_==store_.resourceGeneration()) {
            opened_=true;out=meta_;return {};
        }
        cacheValid_=false;
        entries_.clear();
        spine_.clear();
        titles_.clear();
        txtOffsets_.clear();
        relative_.clear();
        absolute_.clear();
        base_.clear();
        encoding_.clear();
        meta_= {
        };
        absolute_=absolute;
        relative_=path;
        st=store_.hash(path,meta_.identity,[&](size_t done,size_t total){return progress_?progress_("book_hash",done,total):Status{};});
        if(!st) {
            close();
            return st;
        }
        auto ext=lower(path.substr(path.find_last_of('.')==path.npos?path.size():path.find_last_of('.')));
        meta_.engine="paper-local-1";
        meta_.title=path.substr(path.find_last_of('/')+1);
        epub_=ext==".epub";
        if(!epub_) {
            if(ext!=".txt") {
                close();
                return Status::fail(Error::Unsupported,"本地后端当前支持EPUB和TXT");
            }
            FILE*f=fopen(absolute_.c_str(),"rb");
            if(!f) {
                close();
                return Status::fail(Error::Io,"TXT打开失败");
            }
            uint8_t head[3] {
            };
            size_t h=fread(head,1,3,f);
            uint64_t start=0;
            encoding_="utf8";
            if(h>=2&&head[0]==0xff&&head[1]==0xfe) {
                encoding_="utf16le";
                start=2;
            }
            else if(h>=2&&head[0]==0xfe&&head[1]==0xff) {
                encoding_="utf16be";
                start=2;
            }
            else if(h==3&&head[0]==0xef&&head[1]==0xbb&&head[2]==0xbf)start=3;
            fseek(f,0,SEEK_END);
            long total=ftell(f);
            if(total<0||uint64_t(total)>128ull*1024*1024) {
                fclose(f);
                close();
                return Status::fail(Error::TooLarge,"TXT超过128MB");
            }
            if(encoding_!="utf8"&&uint64_t(total)-start>budget_.chapterBytes/2) {
                fclose(f);
                close();
                return Status::fail(Error::TooLarge,"大UTF-16文本请先转换UTF-8");
            }
            txtOffsets_.push_back(start);
            while(encoding_=="utf8"&&txtOffsets_.back()+65536<uint64_t(total)&&txtOffsets_.size()<4096) {
                if(progress_){auto st=progress_("txt_index",txtOffsets_.back(),total);if(!st){fclose(f);close();return st;}}
                uint64_t next=txtOffsets_.back()+65536;
                if(fseek(f,long(next),SEEK_SET)){fclose(f);close();return Status::fail(Error::Io,"TXT索引读取失败");}
                int c=fgetc(f);
                unsigned tail=0;
                while(c!=EOF&&(c&0xc0)==0x80) {
                    if(++tail>3){fclose(f);close();return Status::fail(Error::Corrupt,"TXT字符边界损坏");}
                    ++next;
                    c=fgetc(f);
                }
                txtOffsets_.push_back(next);
            }
            txtOffsets_.push_back(uint64_t(total));
            fclose(f);
            meta_.chapters=txtOffsets_.size()-1;
            for(size_t i=0;i<meta_.chapters;++i)meta_.toc.push_back({"段落 "+std::to_string(i+1),i,""});
            out=meta_;
            cacheValid_=opened_=true;cachedGeneration_=store_.resourceGeneration();
            return {
            };
        }
        st=zipIndex();
        if(!st) {
            close();
            return st;
        }
        std::vector<uint8_t>b;
        st=item("META-INF/container.xml",b,128*1024);
        if(!st) {
            close();
            return st;
        }
        std::string opf;
        st=tokenize(std::string(b.begin(),b.end()),[&](const Token&t){if(t.tag&&!t.close&&t.name=="rootfile"&&opf.empty())opf=attr(t,"full-path");return Status{};},progress_);
        if(!st) { close();return st; }
        if(opf.empty()) {
            close();
            return Status::fail(Error::Corrupt,"EPUB缺少OPF路径");
        }
        st=item(opf,b,budget_.chapterBytes);
        if(!st) {
            close();
            return st;
        }
        base_=opf.find_last_of('/')==opf.npos?"":opf.substr(0,opf.find_last_of('/')+1);
        std::map<std::string,std::string>manifest;
        std::string nav,ncx,current;
        std::vector<std::string>spineIds;
        st=tokenize(std::string(b.begin(),b.end()),[&](const Token&t){if(!t.tag){if(current=="title")meta_.title+=t.text;if(current=="creator")meta_.author+=t.text;return Status{};}if(t.name=="title"||t.name=="creator"){if(t.close)current.clear();else {current=t.name;if(current=="title")meta_.title.clear();}return Status{};}if(t.close)return Status{};if(t.name=="item"){auto id=attr(t,"id"),href=attr(t,"href");std::string full;auto s=archivePath(base_,href,full);if(!s)return s;manifest[id]=full;if(attr(t,"properties").find("nav")!=std::string::npos)nav=full;if(attr(t,"media-type")=="application/x-dtbncx+xml")ncx=full;}if(t.name=="itemref"&&attr(t,"linear")!="no")spineIds.push_back(attr(t,"idref"));return Status{};},progress_);
        if(!st) {
            close();
            return st;
        }
        for(auto&id:spineIds) {
            auto i=manifest.find(id);
            if(i==manifest.end()) {
                close();
                return Status::fail(Error::Corrupt,"EPUB书脊引用不存在");
            }
            spine_.push_back(i->second);
        }
        if(spine_.empty()) {
            close();
            return Status::fail(Error::Corrupt,"EPUB没有章节");
        }
        meta_.chapters=spine_.size();
        titles_.resize(spine_.size());
        auto addToc=[&](const std::string&title,const std::string&link,const std::string&base) {
            std::string href,fragment;
            if(!archivePath(base,link,href,&fragment))return;
            auto it=std::find(spine_.begin(),spine_.end(),href);
            if(it==spine_.end())return;
            size_t index=size_t(it-spine_.begin());
            meta_.toc.push_back({title.empty()?"章节 "+std::to_string(index+1):title,index,fragment});
            if(titles_[index].empty())titles_[index]=title;
        };
        std::string toc=!nav.empty()?nav:ncx;
        Status tocRead;
        if(!toc.empty())tocRead=item(toc,b,budget_.chapterBytes);
        if(tocRead.code==Error::Canceled){close();return tocRead;}
        if(!toc.empty()&&tocRead) {
            std::string base=toc.find_last_of('/')==toc.npos?"":toc.substr(0,toc.find_last_of('/')+1),label,link;
            bool reading=false;
            auto tocParse=tokenize(std::string(b.begin(),b.end()),[&](const Token&t){if(!t.tag){if(reading)label+=t.text;return Status{};}if(!nav.empty()){if(t.name=="a"){if(!t.close){label.clear();link=attr(t,"href");reading=true;}else{reading=false;addToc(label,link,base);}}}else {if(t.name=="navpoint"&&!t.close){label.clear();link.clear();}if(t.name=="text")reading=!t.close;if(t.name=="content"&&!t.close){link=attr(t,"src");addToc(label,link,base);}}return Status{};},progress_);
            if(!tocParse){close();return tocParse;}
        }
        if(meta_.toc.empty())for(size_t i=0;i<spine_.size();++i)meta_.toc.push_back({"章节 "+std::to_string(i+1),i,""});
        out=meta_;
        cacheValid_=opened_=true;cachedGeneration_=store_.resourceGeneration();
        return {
        };
    }
    Status LocalDocument::chapterMarkup(size_t i,std::string&html,std::string&css,std::string&href) {
        if(!opened_||!epub_||i>=spine_.size())return Status::fail(Error::Invalid,"图文章节不存在");
            std::vector<uint8_t>b;
            auto s=item(spine_[i],b,budget_.chapterBytes);
            if(!s)return s;
            html.assign(b.begin(),b.end());href=spine_[i];css.clear();
            bool inStyle=false;size_t sheets=0;
            s=tokenize(html,[&](const Token&t)->Status {
                if(t.tag&&t.name=="style")inStyle=!t.close;
                else if(!t.tag&&inStyle)css+=t.text;
                else if(t.tag&&!t.close&&t.name=="link"&&lower(attr(t,"rel"))=="stylesheet"){
                    if(++sheets>16)return Status::fail(Error::TooLarge,"章节样式表过多");
                    std::string path;auto slash=spine_[i].rfind('/');
                    auto valid=archivePath(slash==std::string::npos?"":spine_[i].substr(0,slash+1),attr(t,"href"),path);
                    if(valid){std::vector<uint8_t> data;auto read=item(path,data,128*1024);
                        if(read){css+='\n';css.append(data.begin(),data.end());}
                        else if(read.code==Error::Canceled)return read;}
                }
                return css.size()>128*1024?Status::fail(Error::TooLarge,"章节样式超过预算"):Status{};
            },progress_);
            return s;
    }
    Status LocalDocument::chapter(size_t i,Chapter&out) {
        if(!opened_||relative_.empty()||i>=meta_.chapters)return Status::fail(Error::Invalid,"章节不存在");
        if(epub_) {
            std::string html,css,href;
            auto s=chapterMarkup(i,html,css,href);
            if(s)s=markupToChapter(html,href,out,budget_.chapterBytes,css,progress_);
            out.title=titles_[i];
            return s;
        }
        FILE*f=fopen(absolute_.c_str(),"rb");
        if(!f)return Status::fail(Error::Io,"TXT读取失败");
        size_t n=size_t(txtOffsets_[i+1]-txtOffsets_[i]);
        std::vector<uint8_t>b(n);
        bool ok=fseek(f,long(txtOffsets_[i]),SEEK_SET)==0&&fread(b.data(),1,n,f)==n;
        fclose(f);
        if(!ok)return Status::fail(Error::Io,"TXT读取不完整");
        out= {
        };
        out.href="txt/"+std::to_string(i);
        if(encoding_=="utf8")out.text.assign(b.begin(),b.end());
        else {
            b.insert(b.begin(),2,0);
            auto s=utf16(b,encoding_=="utf16be",out.text);
            if(!s)return s;
        }
        if(!validUtf8(out.text))return Status::fail(Error::Unsupported,"当前TXT编码未识别，请转换UTF-8或启用CrossMux GBK适配");
        return {
        };
    }
    Status LocalDocument::resource(const std::string&href,std::vector<uint8_t>&out,size_t cap) {
        if(!opened_)return Status::fail(Error::Conflict,"书籍未打开");
        if(!epub_)return Status::fail(Error::Unsupported,"TXT无书内资源");
        std::string safe;
        auto s=archivePath("",href,safe);
        if(!s)return s;
        return item(safe,out,cap);
    }
}
