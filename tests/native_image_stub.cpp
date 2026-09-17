#include "reader/image_stream.h"
namespace reader {
bool DecodeZipEntryImageToL8(ZipReader&,const char*,int,int,RasterImage&,const std::atomic<bool>*){return false;}
}
