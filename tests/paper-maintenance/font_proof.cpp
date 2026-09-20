#include "paper/font_proof.hpp"
#include "paper/text.hpp"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
#include <new>
static bool failNext=false;
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept {
    if(failNext){failNext=false;return nullptr;}
    try{return ::operator new[](size);}catch(...){return nullptr;}
}
namespace fs=std::filesystem;
static uint64_t metric(const paper::PackedFonts& f,const std::string& key){
    auto s=f.statistics();auto at=s.find("\""+key+"\":");assert(at!=s.npos);
    return std::stoull(s.substr(at+key.size()+3));
}
int main(int argc,char**argv){
    assert(argc==4&&!fs::exists(argv[3]));
    auto root=fs::path(argv[3]);fs::create_directories(root/"paper/fonts");
    const std::string relative="paper/fonts/misans-400-26.pgf";
    fs::copy_file(fs::path(argv[1])/"misans-400-26.pgf",root/relative);
    fs::copy_file(fs::path(argv[2])/"misans-400-26.pgf.pfv2",root/(relative+".pfv2"));
    paper::ResourceGate gate;paper::Store store(root.string(),gate);assert(store.initialize());
    for(int px=16;px<=40;++px){
        std::string name="misans-400-"+std::to_string(px)+".pgf";
        if(px!=26){fs::copy_file(fs::path(argv[1])/name,root/"paper/fonts"/name);fs::copy_file(fs::path(argv[2])/(name+".pfv2"),root/"paper/fonts"/(name+".pfv2"));}
        paper::FontProof all;std::vector<uint8_t> checked;assert(all.open(store,"paper/fonts/"+name,px,400,2*1024*1024,checked));
        paper::PackedFonts validated(store);paper::Glyph sample;assert(validated.glyph(0x6e05,{px,400,false},sample));
        assert(validated.statistics().find("\"proof_faces\":1")!=std::string::npos);
    }
    // Production-sized indexes WITH sidecars, sharing one cache as a real page
    // does. The previous test only checked each face in isolation.
    for(int px=16;px<=40;++px){
        paper::PackedFonts shared(store);paper::FontSpec body{px,400,false};
        shared.preferReader(&body);
        for(int round=0;round<12;++round){
            for(int size:{18,px,26}){paper::Glyph g;assert(shared.glyph(0x6e05,{size,400,false},g));}
            if(round==0)continue;
            const auto expected=px==18||px==26?2u:3u;
            assert(metric(shared,"validations")==expected);
            assert(metric(shared,"index_loads")==expected);
            assert(metric(shared,"active_evictions")==0);
        }
    }
    paper::Budget oldBudget;oldBudget.fontIndex=1536*1024;
    paper::PackedFonts oldThrash(store,oldBudget);
    for(int i=0;i<3;++i){assert(oldThrash.validate({18,400,false}));assert(oldThrash.validate({34,400,false}));}
    assert(metric(oldThrash,"index_loads")==6); // reproduces the old device defect
    paper::PackedFonts priority(store);paper::FontSpec active{34,400,false};priority.preferReader(&active);
    assert(priority.validate(active));assert(priority.validate({18,400,false}));assert(priority.validate({26,400,false}));
    assert(priority.validate({40,400,false}));
    auto loads=metric(priority,"index_loads");assert(priority.validate(active));
    assert(metric(priority,"index_loads")==loads&&metric(priority,"active_evictions")==0);
    std::vector<uint8_t> index,bytes;
    paper::FontProof oom;failNext=true;
    assert(oom.open(store,relative,26,400,2*1024*1024,index).code==paper::Error::Unavailable);
    paper::FontProof proof;assert(proof.open(store,relative,26,400,2*1024*1024,index));
    assert(!proof.open(store,relative,25,400,2*1024*1024,index));
    assert(proof.open(store,relative,26,400,2*1024*1024,index));
    FILE*f=fopen((root/relative).c_str(),"rb");assert(f);
    failNext=true;assert(proof.read(f,4090,32,bytes).code==paper::Error::Unavailable);
    assert(proof.read(f,4090,32,bytes));assert(bytes.size()==32);
    for(unsigned n=0;n<20;++n)assert(proof.read(f,n*4096,16,bytes));
    assert(proof.read(f,0,16,bytes));
    assert(!proof.read(f,UINT32_MAX,5,bytes));fclose(f);
    paper::PackedFonts fonts(store);paper::Glyph glyph;assert(fonts.glyph(0x6e05,{26,400,false},glyph));
    assert(fonts.statistics().find("\"proof_faces\":1")!=std::string::npos);
    store.invalidateResources();assert(fonts.glyph(0x6e05,{26,400,false},glyph));
    // Missing sidecars retain the original full-file verification behavior.
    fs::rename(root/(relative+".pfv2"),root/(relative+".saved"));
    paper::PackedFonts fallback(store);assert(fallback.validate({26,400,false}));
    assert(fallback.statistics().find("\"proof_faces\":0")!=std::string::npos);
    fs::rename(root/(relative+".saved"),root/(relative+".pfv2"));
    for(size_t position:{size_t(10),size_t(100)}){
        char original;
        {std::fstream edit(root/relative,std::ios::binary|std::ios::in|std::ios::out);edit.seekg(position);edit.get(original);edit.seekp(position);edit.put(original^1);}
        paper::FontProof changed;assert(!changed.open(store,relative,26,400,2*1024*1024,index));
        {std::fstream edit(root/relative,std::ios::binary|std::ios::in|std::ios::out);edit.seekp(position);edit.put(original);}
    }
    assert(proof.open(store,relative,26,400,2*1024*1024,index));
    // An unused block is not claimed verified at open. Corruption must be
    // rejected when read, and explicit full revalidation must also reject it.
    const size_t at=80+index.size()+3*4096;
    {std::fstream edit(root/relative,std::ios::binary|std::ios::in|std::ios::out);edit.seekg(at);char value;edit.get(value);value^=1;edit.seekp(at);edit.put(value);}
    assert(proof.open(store,relative,26,400,2*1024*1024,index));
    f=fopen((root/relative).c_str(),"rb");assert(f);assert(!proof.read(f,3*4096,16,bytes));fclose(f);
    assert(!fonts.revalidate({26,400,false}));
    {std::fstream edit(root/(relative+".pfv2"),std::ios::binary|std::ios::in|std::ios::out);edit.seekp(100);edit.put('X');}
    paper::FontProof tampered;assert(!tampered.open(store,relative,26,400,2*1024*1024,index));
    std::cout<<"PASS: firmware-pinned proof, index authentication, cross-block glyphs, corruption on access/full revalidation, proof tampering\n";
}
