#pragma once
#include <string>
#include <zlib.h>
#include <brotli/encode.h>

inline bool gzipCompress(const std::string& in,std::string& out){
  z_stream zs{}; if(deflateInit2(&zs,Z_BEST_SPEED,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)!=Z_OK) return false;
  zs.next_in=(Bytef*)in.data(); zs.avail_in=uInt(in.size());
  char buf[1<<14]; int ret; do{ zs.next_out=(Bytef*)buf; zs.avail_out=sizeof(buf);
    ret=deflate(&zs,zs.avail_in?Z_NO_FLUSH:Z_FINISH);
    out.append(buf,sizeof(buf)-zs.avail_out);
  } while(ret==Z_OK); deflateEnd(&zs); return ret==Z_STREAM_END;
}
inline bool brotliCompress(const std::string& in,std::string& out){
  size_t out_len=BrotliEncoderMaxCompressedSize(in.size()); out.resize(out_len);
  auto ok=BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY,BROTLI_DEFAULT_WINDOW,BROTLI_MODE_GENERIC,
    in.size(),(const uint8_t*)in.data(),&out_len,(uint8_t*)out.data());
  if(!ok) return false; out.resize(out_len); return true;
}
