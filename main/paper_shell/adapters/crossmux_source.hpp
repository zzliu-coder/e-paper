#pragma once
#include "paper/document.hpp"
// This source adapter is optional. Build it only against the locked CrossMux
// dependency closure; it does not contain or fake the upstream EPUB engine.
namespace paper {
class CrossMuxSource final:public DocumentSource {
 struct Impl;std::unique_ptr<Impl>impl_;
 public:explicit CrossMuxSource(Store&);~CrossMuxSource()override;
 Status open(const std::string&,Metadata&)override;Status chapter(size_t,Chapter&)override;
 Status resource(const std::string&,std::vector<uint8_t>&,size_t)override;void close()override;
};
}
