#include "book_transfer.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace book_transfer {
namespace {
constexpr size_t kLimit=16*1024*1024, kChunk=1024;
constexpr const char* kRoot="/sdcard/books/";
std::mutex lock;
FILE* file=nullptr;
std::string token, destination, temporary, expected;
size_t total=0, written=0;
int64_t last=0;
mbedtls_sha256_context sha;
std::array<unsigned char,kChunk> buffer;
std::array<char,kChunk*2+1> encoded;
const char* String(cJSON* r,const char* k) { auto* v=cJSON_GetObjectItemCaseSensitive(r,k); return cJSON_IsString(v)?v->valuestring:""; }
bool Number(cJSON* r,const char* k,size_t& n,size_t max=kLimit) {
 auto* v=cJSON_GetObjectItemCaseSensitive(r,k);
 if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>max||floor(v->valuedouble)!=v->valuedouble) return false;
 n=size_t(v->valuedouble); return true;
}
bool Name(const std::string& n) {
 if(n.size()<5||n.size()>80||n[0]=='.') return false;
 for(char c:n) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')) return false;
 return n.find("..") == std::string::npos && (n.substr(n.size()-5)==".epub"||n.substr(n.size()-4)==".txt");
}
int Hex(char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1; }
void Encode(const unsigned char* b,size_t n,char* out) { const char* h="0123456789abcdef"; for(size_t i=0;i<n;++i){out[2*i]=h[b[i]>>4];out[2*i+1]=h[b[i]&15];} out[2*n]=0; }
void Reset(bool remove) {
 if(file) fclose(file);
 file=nullptr;
 if(remove&&!temporary.empty()) unlink(temporary.c_str()); // Only this session's exclusive temporary file.
 mbedtls_sha256_free(&sha); token.clear(); temporary.clear(); destination.clear(); expected.clear(); total=written=0;
}
const char* Run(const char* cmd,cJSON* req,cJSON* reply) {
 if(file && esp_timer_get_time()-last>120000000) Reset(true);
 std::string name=String(req,"name");
 if(!strcmp(cmd,"book.read")) {
   size_t offset;
   if(!Name(name)||!Number(req,"offset",offset)) return "invalid_book_read";
   std::string path=std::string(kRoot)+name; struct stat st{};
   if(stat(path.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_size<0||size_t(st.st_size)>kLimit||offset>size_t(st.st_size)) return "book_unavailable";
   FILE* in=fopen(path.c_str(),"rb"); if(!in) return "book_open_failed";
   if(fseek(in,offset,SEEK_SET)){fclose(in);return "book_seek_failed";}
   size_t n=fread(buffer.data(),1,kChunk,in); bool bad=ferror(in); fclose(in);
   if(bad) return "book_read_failed";
   Encode(buffer.data(),n,encoded.data()); cJSON_AddStringToObject(reply,"hex",encoded.data());
   cJSON_AddNumberToObject(reply,"offset",offset); cJSON_AddNumberToObject(reply,"bytes",n);
   cJSON_AddNumberToObject(reply,"total",st.st_size); cJSON_AddBoolToObject(reply,"eof",offset+n==size_t(st.st_size)); return nullptr;
 }
 if(!strcmp(cmd,"book.begin")) {
   size_t size; std::string digest=String(req,"sha256");
   if(!Name(name)||!Number(req,"size",size)||!size||digest.size()!=64) return "invalid_book_manifest";
   for(char c:digest) if(Hex(c)<0) return "invalid_sha256";
   if(file) return "transfer_busy";
   mkdir("/sdcard/books",0755);
   std::string target=std::string(kRoot)+name; struct stat st{};
   if(!stat(target.c_str(),&st)) return "book_exists_no_overwrite";
   char id[25]; snprintf(id,sizeof(id),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
   std::string temp=std::string(kRoot)+".sdk-upload-"+id+".part";
   int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0) return "book_create_failed";
   file=fdopen(fd,"wb"); if(!file){close(fd);unlink(temp.c_str());return "book_create_failed";}
   token=id; temporary=temp; destination=target; expected=digest; total=size; written=0; last=esp_timer_get_time();
   mbedtls_sha256_init(&sha); if(mbedtls_sha256_starts(&sha,0)){Reset(true);return "sha_failed";}
   cJSON_AddStringToObject(reply,"token",token.c_str()); cJSON_AddNumberToObject(reply,"chunk_max",kChunk); return nullptr;
 }
 if(!file || token!=String(req,"token")) return "unknown_transfer";
 last=esp_timer_get_time();
 if(!strcmp(cmd,"book.abort")){Reset(true);return nullptr;}
 if(!strcmp(cmd,"book.chunk")) {
   size_t offset; const char* hex=String(req,"hex"); size_t length=strlen(hex);
   if(!Number(req,"offset",offset)||offset!=written||length==0||length%2||length>2*kChunk||written+length/2>total) return "invalid_chunk_offset_or_size";
   for(size_t i=0;i<length/2;++i){int a=Hex(hex[2*i]),b=Hex(hex[2*i+1]);if(a<0||b<0)return "invalid_hex";buffer[i]=(a<<4)|b;}
   size_t n=length/2;
   if(fwrite(buffer.data(),1,n,file)!=n||mbedtls_sha256_update(&sha,buffer.data(),n)){Reset(true);return "book_write_failed";}
   written+=n; cJSON_AddNumberToObject(reply,"received",written); return nullptr;
 }
 if(!strcmp(cmd,"book.commit")) {
   if(written!=total) return "incomplete_transfer";
   unsigned char digest[32]; char hex[65];
   if(mbedtls_sha256_finish(&sha,digest)){Reset(true);return "sha_failed";}
   Encode(digest,32,hex);
   if(expected!=hex){Reset(true);return "sha256_mismatch";}
   bool bad=fflush(file)!=0; bad=(fclose(file)!=0)||bad; file=nullptr;
   struct stat st{};
   if(bad){Reset(true);return "book_flush_failed";}
   if(!stat(destination.c_str(),&st)){Reset(true);return "book_exists_no_overwrite";}
   if(rename(temporary.c_str(),destination.c_str())){Reset(true);return "book_publish_failed";}
   cJSON_AddStringToObject(reply,"sha256",hex); cJSON_AddStringToObject(reply,"path",destination.c_str()); cJSON_AddNumberToObject(reply,"bytes",written);
   Reset(false); return nullptr;
 }
 return "unsupported_book_command";
}
}
bool Handle(const char* command,cJSON* request,cJSON* reply) {
 if(strncmp(command,"book.",5))return false;
 std::lock_guard<std::mutex> guard(lock);
 const char* error=Run(command,request,reply); if(error)cJSON_AddStringToObject(reply,"error",error);
 return true;
}
}
