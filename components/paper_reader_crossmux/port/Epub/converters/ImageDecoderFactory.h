#pragma once
#include "ImageToFramebufferDecoder.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "HalStorage.h"
struct ImageDecoderFactory {
 struct Probe final:ImageToFramebufferDecoder {
 bool getDimensions(const std::string&p,ImageDimensions&d)override{HalFile f;if(!Storage.openFileForRead("IMG",p,f))return false;ImageDimsProbe probe;uint8_t buf[1024];while(f.available()){auto n=f.read(buf,sizeof(buf));if(!n)break;if(probe.write(buf,n)!=n)break;}return probe.getDimensions(d);}
 };
 static bool isFormatSupported(const std::string&p){auto i=p.rfind('.');if(i==p.npos)return false;auto e=p.substr(i);return e==".jpg"||e==".jpeg"||e==".png"||e==".JPG"||e==".PNG";}
 static ImageToFramebufferDecoder*getDecoder(const std::string&){static Probe p;return &p;}
};
