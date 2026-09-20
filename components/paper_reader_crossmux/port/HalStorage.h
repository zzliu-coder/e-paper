#pragma once
#include "Print.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
// Reader jobs supply a root validator, own the SD lease, and check
// cancellation.
struct PaperCacheIO {
  std::function<bool(const std::string &, bool)> allowed;
  std::function<bool(size_t, size_t)> progress;
};
inline thread_local PaperCacheIO *paperCacheIO = nullptr;
struct PaperCacheScope {
  PaperCacheIO *old;
  explicit PaperCacheScope(PaperCacheIO &io) : old(paperCacheIO) {
    paperCacheIO = &io;
  }
  ~PaperCacheScope() { paperCacheIO = old; }
};
class HalFile : public Print {
  FILE *f_ = nullptr;
  std::string_view memory_;
  size_t at_ = 0;
  size_t size_ = 0;
  bool memoryOpen_ = false, error_ = false;

public:
  static constexpr size_t MAX_BYTES = 16 * 1024 * 1024;
  HalFile() = default;
  explicit HalFile(std::string_view s) : memory_(s), memoryOpen_(true) {}
  ~HalFile() { close(); }
  HalFile(const HalFile &) = delete;
  HalFile &operator=(const HalFile &) = delete;
  explicit operator bool() const { return isOpen() && !error_; }
  bool isOpen() const { return f_ || memoryOpen_; }
  bool good() const { return !error_; }
  bool open(const std::string &p, const char *mode) {
    close();
    error_ = false;
    if (!paperCacheIO || !paperCacheIO->allowed ||
        !paperCacheIO->allowed(p, mode[0] != 'r'))
      return false;
    f_ = fopen(p.c_str(), mode);
    size_ = 0;
    if (f_) {
      struct stat st{};
      if (fstat(fileno(f_), &st) || st.st_size < 0 ||
          uint64_t(st.st_size) > MAX_BYTES) {
        close();
        return false;
      }
      size_ = size_t(st.st_size);
    }
    return f_;
  }
  bool close() {
    bool ok = !error_;
    if (f_) {
      if (fclose(f_) != 0)
        ok = false;
      f_ = nullptr;
    }
    memoryOpen_ = false;
    memory_ = {};
    at_ = 0;
    return ok;
  }
  size_t position() const {
    if (!f_)
      return at_;
    long p = ftell(f_);
    return p < 0 ? 0 : size_t(p);
  }
  size_t size() const { return f_ ? size_ : memory_.size(); }
  size_t available() const {
    auto n = size(), p = position();
    return n > p ? n - p : 0;
  }
  bool seek(size_t p) {
    if (p > MAX_BYTES)
      return false;
    if (f_) {
      if (fseek(f_, long(p), SEEK_SET) != 0) {
        error_ = true;
        return false;
      }
      return true;
    }
    if (p > memory_.size())
      return false;
    at_ = p;
    return true;
  }
  size_t read(void *out, size_t n) {
    if (error_)
      return 0;
    if (f_) {
      if (paperCacheIO && paperCacheIO->progress &&
          !paperCacheIO->progress(position(), size())) {
        // Cancellation belongs to this operation, not to the underlying file.
        // Exact-length readers reject this read; a later seek/read may retry.
        return 0;
      }
      const auto got = fread(out, 1, n, f_);
      if (ferror(f_))
        error_ = true;
      return got;
    }
    n = std::min(n, available());
    if (n)
      std::memcpy(out, memory_.data() + at_, n);
    at_ += n;
    return n;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const char *p, size_t n) {
    return write(reinterpret_cast<const uint8_t *>(p), n);
  }
  size_t write(const uint8_t *p, size_t n) override {
    if (!f_ || error_ || position() > MAX_BYTES || n > MAX_BYTES - position()) {
      error_ = true;
      return 0;
    }
    auto done = fwrite(p, 1, n, f_);
    size_ = std::max(size_, position());
    if (done != n)
      error_ = true;
    return done;
  }
  bool flush() {
    if (!f_ || fflush(f_)) {
      error_ = true;
      return false;
    }
    return true;
  }
};
struct PaperStorage {
  bool allowed(const std::string &p, bool write) const {
    return paperCacheIO && paperCacheIO->allowed &&
           paperCacheIO->allowed(p, write);
  }
  bool openFileForRead(const char *, const std::string &p, HalFile &f) const {
    return f.open(p, "rb");
  }
  bool openFileForWrite(const char *, const std::string &p, HalFile &f) const {
    return f.open(p, "w+b");
  }
  bool exists(const char *p) const {
    struct stat s{};
    return allowed(p, false) && stat(p, &s) == 0;
  }
  bool mkdir(const char *p) const {
    return allowed(p, true) && (::mkdir(p, 0755) == 0 || exists(p));
  }
  bool remove(const char *p) const {
    return allowed(p, true) && std::remove(p) == 0;
  }
  bool rename(const char *a, const char *b) const {
    return allowed(a, true) && allowed(b, true) && std::rename(a, b) == 0;
  }
};
inline PaperStorage Storage;
