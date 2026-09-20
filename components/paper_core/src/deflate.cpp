#include "paper/document.hpp"
#include <zlib.h>
#include <climits>
namespace paper {
    Status inflateRaw(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, size_t expected, size_t cap,DocumentProgress progress) {
        output.clear();
        if (expected > cap || expected >= UINT_MAX || input.size() > UINT_MAX) return Status::fail(Error::TooLarge, "解压大小超出预算");
        // One guard byte detects streams that claim a smaller uncompressed size.
        output.resize(expected + 1);
        z_stream stream {
        };
        stream.next_in = const_cast<Bytef*>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        stream.next_out = output.data();
        stream.avail_out = static_cast<uInt>(output.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            output.clear();
            return Status::fail(Error::Unavailable, "解压器无法初始化");
        }
        int result=Z_OK;
        do {
            const auto before=stream.total_out,beforeIn=stream.total_in;
            stream.next_out=output.data()+stream.total_out;
            stream.avail_out=static_cast<uInt>(std::min<size_t>(16384,output.size()-stream.total_out));
            result=inflate(&stream,Z_NO_FLUSH);
            if(progress){auto s=progress("decompress",std::min<size_t>(stream.total_out,expected),expected);
                if(!s){inflateEnd(&stream);output.clear();return s;}}
            if(result!=Z_OK||(stream.total_out==before&&stream.total_in==beforeIn)||stream.total_out>=output.size())break;
        }while(true);
        const bool valid = result == Z_STREAM_END && stream.total_out == expected && stream.total_in == input.size();
        inflateEnd(&stream);
        if (!valid) {
            output.clear();
            return Status::fail(Error::Corrupt, "DEFLATE数据无效或长度不符");
        }
        output.resize(expected);
        return {
        };
    }
}
