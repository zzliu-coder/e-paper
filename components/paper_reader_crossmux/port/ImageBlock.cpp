#include "Epub/blocks/ImageBlock.h"
#include "GfxRenderer.h"
#include "Memory.h"
#include "Serialization.h"
void *ImageBlock::extractCtx = nullptr;
ImageBlock::ExtractFn ImageBlock::extractFn = nullptr;
ImageBlock::ImageBlock(const std::string &p, const std::string &s, int16_t w,
                       int16_t h)
    : imagePath(p), srcPath(s), width(w), height(h) {}
void ImageBlock::setExtractor(void *c, ExtractFn f) {
  extractCtx = c;
  extractFn = f;
}
bool ImageBlock::imageExists() const {
  return Storage.exists(imagePath.c_str());
}
bool ImageBlock::hasValidCache() const {
  return false;
} // Pixel ownership stays with PAPER.
bool ImageBlock::needsDecode() const { return true; }
bool ImageBlock::ensureExtracted() {
  return extractFn && extractFn(extractCtx, srcPath.c_str(), imagePath.c_str());
}
void ImageBlock::clearSessionRenderFailures() {}
void ImageBlock::releaseRenderCache() {}
void ImageBlock::renderPlaceholder(GfxRenderer &r, int x, int y) const {
  r.drawLine(x, y, x + width - 1, y, 1, true);
  r.drawLine(x, y + height - 1, x + width - 1, y + height - 1, 1, true);
  r.drawLine(x, y, x + width - 1, y + height - 1, 1, true);
}
void ImageBlock::render(GfxRenderer &r, int x, int y) {
  render(r, x, y, PixelCachePolicy::Stream);
}
bool ImageBlock::render(GfxRenderer &r, int x, int y, PixelCachePolicy) {
  if (ensureExtracted() && r.image && r.image(imagePath, x, y, width, height))
    return true;
  renderPlaceholder(r, x, y);
  return false;
}
bool ImageBlock::cacheDecodedImage(GfxRenderer &, int, int) {
  return ensureExtracted();
}
bool ImageBlock::serialize(HalFile &f) {
  serialization::writeString(f, imagePath);
  serialization::writeString(f, srcPath);
  serialization::writePod(f, width);
  serialization::writePod(f, height);
  return f.good();
}
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile &f) {
  std::string p, s;
  int16_t w, h;
  if (!serialization::readString(f, p, 1024) ||
      !serialization::readString(f, s, 1024) || !serialization::readPod(f, w) ||
      !serialization::readPod(f, h) || w < 1 || h < 1 || w > 480 || h > 800)
    return {};
  return makeUniqueNoThrow<ImageBlock>(p, s, w, h);
}
