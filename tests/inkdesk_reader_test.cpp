#include "inkdesk_reader.h"
#include "crossmux_txt/TxtParagraph.h"
#include "crossmux_txt/TxtPageIndex.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <fstream>
int main(int argc,char** argv) {
 uint8_t gbk[32]={0xd6,0xd0,0xce,0xc4};
 assert(txt_encoding::detect(gbk,4,true)==txt_encoding::Encoding::Gbk);
 auto converted=txt_encoding::transcodeGbkInPlace(gbk,4,32,true);
 assert(converted.rawLength==4 && converted.utf8Length==6 && !strcmp(reinterpret_cast<char*>(gbk),"中文"));
 assert(txt_encoding::gbkSourceLength(gbk,6)==4);
 auto info=txt_paragraph::analyzeLine(reinterpret_cast<const uint8_t*>("  text"),6);
 assert(info.kind==txt_paragraph::LineKind::Indented && info.contentOffset==2);
 InkDeskReader r; r.Library(); r.Tap(100,150);
 auto* status=cJSON_CreateObject(); r.Status(status);
 assert(!cJSON_IsTrue(cJSON_GetObjectItem(status,"library")));
 assert(cJSON_GetObjectItem(status,"next_offset")->valuedouble>0);
 cJSON_Delete(status);
 inkdesk::Frame frame; r.Draw(frame); assert(!frame.overflow && frame.drawCount>15);
 r.Next(); status=cJSON_CreateObject(); r.Status(status);
 assert(cJSON_GetObjectItem(status,"page")->valueint==2);
 assert(cJSON_GetObjectItem(status,"offset")->valuedouble>0); cJSON_Delete(status);
 r.Previous(); status=cJSON_CreateObject(); r.Status(status);
 assert(cJSON_GetObjectItem(status,"page")->valueint==1); cJSON_Delete(status);
 for(int n=0;n<100;++n) r.Next();
 status=cJSON_CreateObject(); r.Status(status);
 assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"eof"))); cJSON_Delete(status);
 puts("PASS: GBK, paragraph, file open, bounded draw, forward/backward pages, EOF");
 if(argc>1){
   std::string path=INKDESK_SD_ROOT "/books"; mkdir(path.c_str(),0755); path+="/test.epub";
   {std::ifstream input(argv[1],std::ios::binary); std::ofstream output(path,std::ios::binary);output<<input.rdbuf();}
   r.Library();r.Tap(100,150);
   status=cJSON_CreateObject();r.Status(status);
   assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"loaded")));
   assert(!strcmp(cJSON_GetObjectItem(status,"format")->valuestring,"EPUB"));
   assert(cJSON_GetObjectItem(status,"chapters")->valueint>1);cJSON_Delete(status);
   int pages=0;
   for(;pages<4096;++pages){
     status=cJSON_CreateObject();r.Status(status);
     assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"loaded")));
     bool eof=cJSON_IsTrue(cJSON_GetObjectItem(status,"eof"));cJSON_Delete(status);
     r.Draw(frame);assert(!frame.overflow);
     if(eof)break;
     r.Next();
   }
   assert(pages>1&&pages<4096);
   for(int n=0;n<pages;++n)r.Previous();
   status=cJSON_CreateObject();r.Status(status);
   assert(cJSON_GetObjectItem(status,"page")->valueint==1);
   assert(cJSON_GetObjectItem(status,"chapter")->valueint==1);
   assert(cJSON_IsTrue(cJSON_GetObjectItem(status,"loaded")));cJSON_Delete(status);
   printf("PASS: actual EPUB %d pages forward to EOF and backward to first chapter\n",pages+1);
 }
}
