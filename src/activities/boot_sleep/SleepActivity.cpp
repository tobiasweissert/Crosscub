#include "SleepActivity.h"

#include <BitmapHelpers.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
// PNGdec's bundled zlib exports `local`; keep it out of subsequent SDK UI headers.
#pragma push_macro("local")
#undef local
#include <PNGdec.h>
#pragma pop_macro("local")
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

#include "../home/RecentBookProgress.h"
#include "../reader/BookStatsView.h"
#include "../reader/EpubReaderActivity.h"
#include "../reader/EpubReaderUtils.h"
#include "../reader/TxtReaderActivity.h"
#include "../reader/XtcReaderActivity.h"
#include "AppCapabilities.h"
#include "AppVersion.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ImageFolderIndex.h"
#include "RecentBooksStore.h"
#include "SleepCoverAssets.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "components/themes/dashboard/DashboardTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "fontIds.h"
#include "images/CrosscubLogo.h"
#include "images/MoonIcon.h"

namespace {

constexpr bool TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH = true;
constexpr int sleepBuildInfoSideMargin = 20;

HalDisplay::GrayscaleMode sleepGrayscaleMode(const GfxRenderer& renderer) {
  return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
             ? HalDisplay::GrayscaleMode::Direct
             : HalDisplay::GrayscaleMode::Absolute;
}

// Kept separate from normal custom sleep sources so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
        case TransparentOverlayPass::GrayscaleMsb: {
          const auto planePixel =
              grayPlanePixel(level, pass == TransparentOverlayPass::GrayscaleMsb, renderer.grayPlanesAreAbsolute());
          if (planePixel.write) renderer.drawPixel(screenX, screenY, planePixel.black);
          break;
        }
      }
    }
  }

  return true;
}

// Starts a gray plane under an overlay. Absolute planes keep the B/W base; other gray planes carry only
// the overlay's gray. Cover + Transparent redraws a gray cover (set only for absolute planes) to keep its gray levels.
// Overlay gray leaves a mask plane in the framebuffer, so a later B/W update of
// the retained sleep frame (the Picco USB charge strip) needs the B/W frame back.
// Absolute planes must keep the gray baseline: restoring the B/W frame there
// makes the next fast refresh drop the grays (Picco SSD1677).
class BwFrameKeeper {
 public:
  BwFrameKeeper(GfxRenderer& renderer, const bool keep)
      : renderer(renderer), keep(keep), stored(!keep || renderer.storeBwBuffer()) {}
  ~BwFrameKeeper() {
    if (keep && stored) renderer.restoreBwBuffer();
  }
  // False when the B/W frame could not be kept; callers must skip the gray passes.
  bool ready() const { return stored; }
  BwFrameKeeper(const BwFrameKeeper&) = delete;
  BwFrameKeeper& operator=(const BwFrameKeeper&) = delete;

 private:
  GfxRenderer& renderer;
  const bool keep;
  const bool stored;
};

bool beginOverlayGrayPlane(GfxRenderer& renderer, const Bitmap* grayCover, const GfxRenderer::RenderMode plane,
                           const bool absolute) {
  if (grayCover) {
    if (grayCover->rewindToData() != BmpReaderError::Ok) return false;
    renderer.clearScreen();
  } else if (!absolute) {
    renderer.clearScreen(0x00);
  }
  renderer.setRenderMode(plane);
  if (!grayCover) return true;
  const auto placement = calculateBitmapPlacement(grayCover->getWidth(), grayCover->getHeight(), renderer);
  return renderer.drawBitmap(*grayCover, placement.x, placement.y, renderer.getScreenWidth(),
                             renderer.getScreenHeight(), placement.cropX, placement.cropY);
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog,
                                                  const Bitmap* grayCover) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderer.storeBwBuffer()) return AlphaOverlayResult::Error;
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW)) {
    renderer.restoreBwBuffer(false);
    return AlphaOverlayResult::Error;
  }
  renderer.discardStoredBwBuffer();
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return AlphaOverlayResult::Rendered;
  }
  if (absolute) {
    // Retain owner single HALF-refresh policy.
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return AlphaOverlayResult::Error;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!beginOverlayGrayPlane(renderer, grayCover, GfxRenderer::GRAYSCALE_LSB, absolute) ||
      !renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Keep the current display instead of trying another overlay with a
    // framebuffer that now contains an incomplete gray plane.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!beginOverlayGrayPlane(renderer, grayCover, GfxRenderer::GRAYSCALE_MSB, absolute) ||
      !renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

void getLCGState(const SleepRecentKind kind, uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                 uint16_t& indexesTotal) {
  switch (kind) {
    case SleepRecentKind::Overlay:
      APP_STATE.getSleepOverlayLCG(seed, state, indexesLeft, indexesTotal);
      break;
    case SleepRecentKind::Standard:
      APP_STATE.getSleepLCG(seed, state, indexesLeft, indexesTotal);
      break;
  }
}

void setLCGState(const SleepRecentKind kind, uint32_t seed, uint32_t state, uint16_t indexesLeft,
                 uint16_t indexesTotal) {
  switch (kind) {
    case SleepRecentKind::Overlay:
      APP_STATE.setSleepOverlayLCG(seed, state, indexesLeft, indexesTotal);
      break;
    case SleepRecentKind::Standard:
      APP_STATE.setSleepLCG(seed, state, indexesLeft, indexesTotal);
      break;
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* firstDirPath, const char* secondDirPath, const SleepRecentKind recentKind,
                           std::string& selectedPath) {
  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  constexpr size_t DIRECTORY_COUNT = 2;
  const char* dirPaths[DIRECTORY_COUNT] = {firstDirPath, secondDirPath};
  uint16_t directoryCounts[DIRECTORY_COUNT] = {};
  uint16_t fileCount = 0;
  for (size_t i = 0; i < DIRECTORY_COUNT; i++) {
    auto dir = Storage.open(dirPaths[i]);
    if (!dir || !dir.isDirectory()) continue;

    const auto remainingCapacity = static_cast<uint16_t>(UINT16_MAX - fileCount);
    while (directoryCounts[i] < remainingCapacity && findNextValidSleepImage(dir, recentKind, name.get())) {
      directoryCounts[i]++;
    }
    fileCount = static_cast<uint16_t>(fileCount + directoryCounts[i]);
  }

  if (fileCount == 0) return false;

  uint16_t randomFileIndex = 0;

  // Hull-Dobell theorem conditions for full period over mod 2^k: odd increment c and multiplier a = 5
  // (a - 1 divisible by 4) give period m for any seed; m = 1 is trivial.
  // https://en.wikipedia.org/wiki/Linear_congruential_generator#Period_length
  // Cycle-walking skips states >= fileCount, visiting every image once per cycle and finding a valid index
  // within at most lcgM steps.
  uint16_t lcgIndexesLeft, lcgIndexesTotal;
  uint32_t lcgSeed, lcgState;
  uint32_t lcgM = 1;

  getLCGState(recentKind, lcgSeed, lcgState, lcgIndexesLeft, lcgIndexesTotal);

  while (lcgM < fileCount) {
    lcgM <<= 1;
  }

  uint32_t lcgC = (lcgSeed * 2u + 1u) % lcgM;

  if (lcgIndexesLeft == 0 || lcgIndexesTotal != fileCount || lcgIndexesLeft > fileCount || lcgState >= lcgM) {
    uint16_t previousIndexes[CrossPointState::SLEEP_AVOID_REPEAT_LOOKBACK];
    uint8_t previousCount = 0;
    const uint8_t window =
        static_cast<uint8_t>(std::min<uint16_t>(fileCount / 2, CrossPointState::SLEEP_AVOID_REPEAT_LOOKBACK));
    if (lcgIndexesLeft == 0 && lcgIndexesTotal == fileCount && lcgState < fileCount) {
      // Invert the completed cycle (5 * 0xCCCCCCCD = 1 mod 2^32) to obtain recently shown image indexes
      static constexpr uint32_t LCG_MULTIPLIER_INVERSE = 0xCCCCCCCDu;
      uint32_t previousState = lcgState;
      for (uint32_t step = 0; step < lcgM && previousCount < window; ++step) {
        if (previousState < fileCount) previousIndexes[previousCount++] = static_cast<uint16_t>(previousState);
        previousState = (LCG_MULTIPLIER_INVERSE * (previousState - lcgC)) & (lcgM - 1u);
      }
    }

    lcgIndexesLeft = lcgIndexesTotal = fileCount;
    const auto isPreviousIndex = [&](uint32_t index) {
      return std::find(previousIndexes, previousIndexes + previousCount, index) != previousIndexes + previousCount;
    };

    // Find a new seed that
    // 1. doesn't immediately repeat an image
    // 2. has the fewest overlaps with previousIndexes in the remaining first "window" generations
    // 3. ideally isn't the same seed as the previous one
    uint8_t bestOverlapCount = window + 1;
    const uint32_t prevLcgSeed = lcgSeed;
    static constexpr uint8_t MAX_SEED_ATTEMPTS = 16;
    for (uint8_t attempt = 0; attempt < MAX_SEED_ATTEMPTS; ++attempt) {
      const uint32_t candidateSeed = static_cast<uint32_t>(random(lcgM));
      const uint32_t candidateC = (candidateSeed * 2u + 1u) % lcgM;
      uint32_t candidateState = candidateSeed;
      for (uint32_t step = 0; step < lcgM; ++step) {
        candidateState = (5u * candidateState + candidateC) % lcgM;
        if (candidateState < fileCount && !isPreviousIndex(candidateState)) break;
      }

      const uint32_t firstState = candidateState;
      uint8_t overlapCount = 0;

      if (previousCount > 0 && candidateSeed == prevLcgSeed) {
        // assume some overlaps for the previous seed to penalize it for selection.
        overlapCount = window > 1 ? window / 2 : 1;
      } else {
        uint8_t generatedCount = 1;
        for (uint32_t step = 0; step < lcgM && generatedCount < window && previousCount > 0; ++step) {
          candidateState = (5u * candidateState + candidateC) % lcgM;
          if (candidateState >= fileCount) continue;
          ++generatedCount;
          if (isPreviousIndex(candidateState)) ++overlapCount;
        }
      }

      if (overlapCount < bestOverlapCount) {
        bestOverlapCount = overlapCount;
        lcgSeed = candidateSeed;
        lcgState = firstState;
      }
      if (overlapCount == 0) break;
    }
    randomFileIndex = static_cast<uint16_t>(lcgState);
  } else {
    for (uint32_t attempt = 0; attempt < lcgM; ++attempt) {
      lcgState = (5u * lcgState + lcgC) % lcgM;
      if (lcgState < fileCount) {
        randomFileIndex = lcgState;
        break;
      }
    }
  }

  size_t directoryIndex = 0;
  uint16_t localIndex = randomFileIndex;
  if (randomFileIndex >= directoryCounts[0]) {
    directoryIndex = 1;
    localIndex = static_cast<uint16_t>(randomFileIndex - directoryCounts[0]);
  }

  auto dir = Storage.open(dirPaths[directoryIndex]);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("SLP", "Failed to open selected sleep image directory: %s", dirPaths[directoryIndex]);
    return false;
  }
  for (uint16_t index = 0; index <= localIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  const char* selectedDirPath = dirPaths[directoryIndex];
  selectedPath.reserve(strlen(selectedDirPath) + 1 + strlen(name.get()));
  selectedPath = selectedDirPath;
  selectedPath += "/";
  selectedPath += name.get();
  setLCGState(recentKind, lcgSeed, lcgState, lcgIndexesLeft - 1, lcgIndexesTotal);
  APP_STATE.saveToFile();
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
}

bool sleepCoverFilterInvertsGeneratedScreen() {
  return SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE;
}

void hideOverlayBatteryStrip(const GfxRenderer& renderer) {
  if (!SETTINGS.statusBarBattery) {
    return;
  }

  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  const int textY = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - 4;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  // Reserve the full left-side status indicator lane used by bookmark + battery.
  // This keeps chapter/progress text readable while removing the battery glance target.
  static constexpr int bookmarkReserveWidth = 13;  // bookmark width + gap from BaseTheme::drawStatusBar()
  static constexpr int batteryPercentSpacing = 4;  // matches BaseTheme::batteryPercentSpacing
  const int clearWidth =
      bookmarkReserveWidth + metrics.batteryWidth +
      (showBatteryPercentage ? batteryPercentSpacing + renderer.getTextWidth(SMALL_FONT_ID, "100%") : 0);
  const int clearHeight = std::max(renderer.getTextHeight(SMALL_FONT_ID), metrics.batteryHeight + 6);

  renderer.fillRect(metrics.statusBarHorizontalMargin + orientedMarginLeft + 1, textY, clearWidth, clearHeight, false);
}

// Context passed through PNGdec's decode() user-pointer to the per-scanline draw callback.
struct PngOverlayCtx {
  GfxRenderer* renderer;
  int screenW;
  int screenH;
  int srcWidth;
  int dstWidth;
  int dstX;
  int dstY;
  float yScale;
  int lastDstY;
  // Color-key transparency (tRNS chunk) for TRUECOLOR and GRAYSCALE images.
  // Initialized lazily on the first draw callback because tRNS is processed during decode(),
  // not during open() — so hasAlpha()/getTransparentColor() are only valid once decode() starts.
  // -2 = not yet read; -1 = no color key; >=0 = 0x00RRGGBB (TRUECOLOR) or low-byte gray.
  int32_t transparentColor;
  PNG* pngObj;  // for lazy-init of transparentColor on first callback
};

// PNGdec file I/O callbacks — mirror the pattern in PngToFramebufferConverter.cpp.
void* pngSleepOpen(const char* filename, int32_t* size) {
  HalFile* f = new HalFile();
  if (!Storage.openFileForRead("SLP", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}
void pngSleepClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}
int32_t pngSleepRead(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  return f ? f->read(pBuf, len) : 0;
}
int32_t pngSleepSeek(PNGFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

// Per-scanline draw callback for PNG overlay compositing.
// Transparent pixels (alpha < 128) are skipped so the reader page shows through.
// Opaque pixels are drawn in their grayscale brightness (dark → black, light → white).
int pngOverlayDraw(PNGDRAW* pDraw) {
  PngOverlayCtx* ctx = reinterpret_cast<PngOverlayCtx*>(pDraw->pUser);

  // Lazy-init: tRNS chunk is processed during decode() before any IDAT data, so by the time
  // the first draw callback fires, hasAlpha() / getTransparentColor() are already valid.
  if (ctx->transparentColor == -2) {
    const int pt = pDraw->iPixelType;
    ctx->transparentColor = (pDraw->iHasAlpha && (pt == PNG_PIXEL_TRUECOLOR || pt == PNG_PIXEL_GRAYSCALE))
                                ? static_cast<int32_t>(ctx->pngObj->getTransparentColor())
                                : -1;
  }

  const int destY = ctx->dstY + (int)(pDraw->y * ctx->yScale);
  if (destY == ctx->lastDstY) return 1;  // skip duplicate rows from Y scaling
  ctx->lastDstY = destY;
  if (destY < 0 || destY >= ctx->screenH) return 1;

  const int srcWidth = ctx->srcWidth;
  const int dstWidth = ctx->dstWidth;
  const uint8_t* pixels = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;
  const int hasAlpha = pDraw->iHasAlpha;

  int srcX = 0, error = 0;
  for (int dstX = 0; dstX < dstWidth; dstX++) {
    const int outX = ctx->dstX + dstX;
    if (outX >= 0 && outX < ctx->screenW) {
      uint8_t alpha = 255, gray = 0;
      switch (pixelType) {
        case PNG_PIXEL_TRUECOLOR_ALPHA: {
          const uint8_t* p = &pixels[srcX * 4];
          alpha = p[3];
          gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          break;
        }
        case PNG_PIXEL_GRAY_ALPHA:
          gray = pixels[srcX * 2];
          alpha = pixels[srcX * 2 + 1];
          break;
        case PNG_PIXEL_TRUECOLOR: {
          const uint8_t* p = &pixels[srcX * 3];
          gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          // tRNS color-key: if pixel matches the designated transparent color, skip it
          if (ctx->transparentColor >= 0 && p[0] == (uint8_t)((ctx->transparentColor >> 16) & 0xFF) &&
              p[1] == (uint8_t)((ctx->transparentColor >> 8) & 0xFF) &&
              p[2] == (uint8_t)(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        }
        case PNG_PIXEL_GRAYSCALE:
          gray = pixels[srcX];
          // tRNS color-key: transparent gray value stored in low byte
          if (ctx->transparentColor >= 0 && gray == (uint8_t)(ctx->transparentColor & 0xFF)) {
            alpha = 0;
          }
          break;
        case PNG_PIXEL_INDEXED:
          if (pDraw->pPalette) {
            const uint8_t idx = pixels[srcX];
            const uint8_t* p = &pDraw->pPalette[idx * 3];
            gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            if (hasAlpha) alpha = pDraw->pPalette[768 + idx];
          }
          break;
        default:
          gray = pixels[srcX];
          break;
      }

      if (alpha >= 128) {
        const auto mode = ctx->renderer->getRenderMode();
        if (mode == GfxRenderer::BW) {
          ctx->renderer->drawPixel(outX, destY, gray < 128);  // true = black, false = white
        } else {
          const auto pixel =
              grayPlanePixel(gray >> 6, mode == GfxRenderer::GRAYSCALE_MSB, ctx->renderer->grayPlanesAreAbsolute());
          if (pixel.write) ctx->renderer->drawPixel(outX, destY, pixel.black);
        }
      }
      // alpha < 128: transparent — leave the reader page pixel intact
    }

    // Bresenham-style X stepping (handles downscaling; 1:1 when srcWidth == dstWidth)
    error += srcWidth;
    while (error >= dstWidth) {
      error -= dstWidth;
      srcX++;
    }
  }
  return 1;
}

std::string filenameFromPath(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
}

std::string recentTitleForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto book = std::find_if(books.begin(), books.end(), [&path](const RecentBook& candidate) {
    return candidate.path == path && !candidate.title.empty();
  });
  return book == books.end() ? std::string{} : book->title;
}

RecentBook recentBookForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto book =
      std::find_if(books.begin(), books.end(), [&path](const RecentBook& candidate) { return candidate.path == path; });
  if (book != books.end()) {
    return *book;
  }

  RecentBook loadedBook = RECENT_BOOKS.getDataFromBook(path);
  if (loadedBook.title.empty()) {
    loadedBook.title = filenameFromPath(path);
  }
  return loadedBook;
}

std::string bookStatsCachePathFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub::cachePathForFilePath(path, "/.crosspoint");
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, "/.crosspoint").getCachePath();
  }
  return {};
}

BookReadingStats loadBookStatsForPath(const std::string& path) {
  const std::string cachePath = bookStatsCachePathFor(path);
  if (cachePath.empty()) {
    return BookReadingStats{};
  }
  return BookReadingStats::load(cachePath);
}

std::string loadChapterTitleForPath(const std::string& path) {
  if (!FsHelpers::hasEpubExtension(path)) {
    return {};
  }

  Epub epub(path, "/.crosspoint");
  if (!epub.load(false, true, Epub::XLocationLoadMode::Skip)) {
    return {};
  }

  EpubReaderUtils::Progress progress;
  if (!EpubReaderUtils::loadProgress(epub, progress, "SLP")) {
    return {};
  }

  const auto spineItem = epub.getSpineItem(progress.spineIndex);
  if (spineItem.tocIndex < 0) {
    return {};
  }

  const auto tocItem = epub.getTocItem(spineItem.tocIndex);
  return tocItem.title;
}

enum class OverlayDrawResult : uint8_t { NotFound, Drawn, Failed };

enum class SleepImageMode : uint8_t { Custom, Overlay };

struct SleepImageSelection {
  std::string path;
  bool isPng = false;
};

bool isBmpSleepImagePath(const std::string& path) { return FsHelpers::hasBmpExtension(path); }

bool isPngSleepImagePath(const std::string& path) { return FsHelpers::hasPngExtension(path); }

bool tryOpenSleepDirectory(HalFile& dir, std::string& sleepDir, const std::string& candidate) {
  if (candidate.empty()) {
    return false;
  }

  dir = Storage.open(candidate.c_str());
  if (dir && dir.isDirectory()) {
    sleepDir = candidate;
    return true;
  }

  if (dir) {
    dir.close();
  }
  return false;
}

bool resolvePreferredSleepDirectory(std::string& sleepDir) {
  sleepDir.clear();

  const auto folderExists = [&sleepDir](const std::string& candidate) {
    if (candidate.empty() || !Storage.exists(candidate.c_str())) return false;
    sleepDir = candidate;
    return true;
  };

  if (folderExists(APP_STATE.preferredSleepFolderPath)) return true;

  if (!APP_STATE.preferredSleepFolderPath.empty()) {
    LOG_INF("SLP", "Preferred sleep folder missing, falling back: %s", APP_STATE.preferredSleepFolderPath.c_str());
  }

  char defaultSleepDir[16];
  if (FsHelpers::resolveRootDirectoryIgnoreCase("/.sleep", defaultSleepDir, sizeof(defaultSleepDir)) &&
      folderExists(defaultSleepDir)) {
    return true;
  }
  return FsHelpers::resolveRootDirectoryIgnoreCase("/sleep", defaultSleepDir, sizeof(defaultSleepDir)) &&
         folderExists(defaultSleepDir);
}

bool openPreferredSleepDirectory(HalFile& dir, std::string& sleepDir) {
  if (!resolvePreferredSleepDirectory(sleepDir)) return false;

  return tryOpenSleepDirectory(dir, sleepDir, sleepDir);
}

bool selectPinnedSleepImage(SleepImageMode mode, SleepImageSelection& selection) {
  const std::string& favorite = APP_STATE.favoriteSleepImagePath;
  if (favorite.empty()) {
    return false;
  }

  if (!Storage.exists(favorite.c_str())) {
    LOG_INF("SLP", "Pinned sleep image missing, falling back: %s", favorite.c_str());
    return false;
  }

  if (isBmpSleepImagePath(favorite)) {
    selection.path = favorite;
    selection.isPng = false;
    return true;
  }

  if (isPngSleepImagePath(favorite)) {
    if (mode == SleepImageMode::Overlay) {
      selection.path = favorite;
      selection.isPng = true;
      return true;
    }

    LOG_INF("SLP", "Pinned PNG sleep image requires Page Overlay mode, falling back: %s", favorite.c_str());
    return false;
  }

  LOG_ERR("SLP", "Pinned sleep image has unsupported extension: %s", favorite.c_str());
  return false;
}

bool selectRandomSleepImage(SleepImageMode mode, SleepImageSelection& selection, bool validateBmpHeaders = false,
                            bool bmpOnly = false) {
  HalFile dir;
  std::string sleepDir;
  if (!resolvePreferredSleepDirectory(sleepDir)) return false;

  const bool allowPng = mode == SleepImageMode::Overlay && !bmpOnly;
  ImageFolderIndex::Selection indexedSelection;
  if (ImageFolderIndex::select(sleepDir, allowPng, validateBmpHeaders, APP_STATE.recentSleepImages,
                               CrossPointState::SLEEP_RECENT_COUNT, APP_STATE.recentSleepPos, APP_STATE.recentSleepFill,
                               std::min(APP_STATE.recentSleepFill, CrossPointState::SLEEP_RECENT_COUNT),
                               indexedSelection)) {
    selection.path = std::move(indexedSelection.path);
    selection.isPng = indexedSelection.isPng;
    APP_STATE.pushRecentSleep(indexedSelection.index);
    APP_STATE.saveToFile();
    return true;
  }

  // Cache creation is best-effort. Reopen the directory only for the legacy
  // reservoir fallback when the cache could not be loaded or written.
  if (!openPreferredSleepDirectory(dir, sleepDir)) return false;

  // Keep one reservoir for every candidate and one that excludes recent images.
  // This avoids holding the whole directory in RAM or opening every BMP just to
  // parse its header before picking one.
  std::string nonRecentPath;
  uint16_t candidateCount = 0;
  uint16_t selectedIndex = 0;
  uint16_t nonRecentCount = 0;
  uint16_t nonRecentIndex = 0;
  const uint8_t recentWindow = std::min(APP_STATE.recentSleepFill, CrossPointState::SLEEP_RECENT_COUNT);
  const auto setSleepImagePath = [&](std::string& path, std::string_view filename) {
    path = sleepDir;
    path += '/';
    path.append(filename.data(), filename.size());
  };
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    file.getName(name, sizeof(name));
    const std::string_view filename(name);
    if (filename.empty() || filename.front() == '.') {
      file.close();
      continue;
    }

    const bool isBmp = FsHelpers::hasBmpExtension(filename);
    const bool isPng = allowPng && FsHelpers::hasPngExtension(filename);
    if (!isBmp && !isPng) {
      file.close();
      continue;
    }

    // The normal path defers this SD read until after selection so folders with
    // many wallpapers stay fast. Only use the slower validation pass after a
    // selected BMP failed to render.
    if (isBmp && validateBmpHeaders) {
      Bitmap bitmap(file);
      const BmpReaderError parseResult = bitmap.parseHeaders();
      if (parseResult != BmpReaderError::Ok) {
        LOG_ERR("SLP", "Skipping invalid BMP sleep image %s/%.*s: %s", sleepDir.c_str(),
                static_cast<int>(filename.size()), filename.data(), Bitmap::errorToString(parseResult));
        file.close();
        continue;
      }
    }

    if (candidateCount == UINT16_MAX) {
      file.close();
      continue;
    }

    candidateCount++;
    const uint16_t candidateIndex = candidateCount - 1;
    if (random(candidateCount) == 0) {
      setSleepImagePath(selection.path, filename);
      selectedIndex = candidateIndex;
    }

    if (!APP_STATE.isRecentSleep(candidateIndex, recentWindow)) {
      nonRecentCount++;
      if (random(nonRecentCount) == 0) {
        setSleepImagePath(nonRecentPath, filename);
        nonRecentIndex = candidateIndex;
      }
    }
    file.close();
  }
  dir.close();

  if (candidateCount == 0) {
    return false;
  }

  if (nonRecentCount > 0) {
    selection.path = std::move(nonRecentPath);
    selectedIndex = nonRecentIndex;
  }

  // With fewer images than the recent-history window, every candidate can be
  // recent. Fall back to the all-candidates reservoir so a custom sleep screen
  // still renders.
  APP_STATE.pushRecentSleep(selectedIndex);
  APP_STATE.saveToFile();
  selection.isPng = FsHelpers::hasPngExtension(selection.path);
  return true;
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();
  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  // Sleep screens draw directly, outside ActivityManager's normal render path.
  // Quick Resume retains the current screen, so preserve its Night Mode output;
  // generated sleep screens continue to use their normal polarity.
  const bool frameWasInverted = display.isInverted();
  display.setInverted(renderQuickResume && SETTINGS.screenInverted != 0);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  const auto sleepScreen = SETTINGS.sleepScreen;
  if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM ||
      sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT) {
    // No destructive popup: these two modes retain the actual visible frame.
    // Materialize output-only night polarity before returning the driver to normal.
    if (frameWasInverted) renderer.invertScreen();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    releaseSdFontCachesForDecode(renderer);
    if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT) return renderCoverSleepScreen();
    return renderTransparentCustomSleepScreen();
  }

  const bool sleepScreenUsesRecentBooks = sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP ||
                                          sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_SLEEP ||
                                          sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS_SLEEP ||
                                          sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::DASHBOARD_SLEEP;
  const std::string& recentBookPath = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (sleepScreenUsesRecentBooks && !recentBookPath.empty()) {
    RECENT_BOOKS.ensureLoaded();
  }

  overlayBackgroundBufferStored =
      sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY && renderer.storeBwBuffer();

  // Render only the final sleep frame; avoid a transient high-contrast update.
  const bool showSleepPopup = true;
  // Show the popup in the orientation that was visible before reader exit restores
  // global settings. Reset to portrait afterwards so sleep screen layout stays unchanged.
  if (APP_STATE.lastSleepFromReader) {
    if (showSleepPopup) {
      renderer.setOrientation(sleepPopupOrientation);
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else if (showSleepPopup) {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY):
      return renderOverlaySleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP):
      return renderReadingStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_SLEEP):
      return renderMinimalSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS_SLEEP):
      return renderMinimalStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::DASHBOARD_SLEEP):
      return renderDashboardSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  const auto tryRenderSelection = [this](const SleepImageSelection& selection) {
    HalFile file;
    if (!Storage.openFileForRead("SLP", selection.path, file)) {
      LOG_ERR("SLP", "Failed to open custom sleep image: %s", selection.path.c_str());
      return false;
    }

    LOG_INF("SLP", "Loading custom sleep image: %s", selection.path.c_str());
    delay(100);
    // Use image-specific gray levels only when the panel accepts complete planes.
    Bitmap bitmap(file, true,
                  renderer.supportsAbsoluteGrayscale() &&
                      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
    const BmpReaderError parseResult = bitmap.parseHeaders();
    if (parseResult != BmpReaderError::Ok) {
      LOG_ERR("SLP", "Failed to parse custom sleep BMP %s: %s", selection.path.c_str(),
              Bitmap::errorToString(parseResult));
      file.close();
      return false;
    }

    const bool success = renderBitmapSleepScreen(bitmap);
    file.close();
    return success;
  };

  SleepImageSelection selection;
  if (selectPinnedSleepImage(SleepImageMode::Custom, selection) && tryRenderSelection(selection)) {
    return;
  }

  if (selectRandomSleepImage(SleepImageMode::Custom, selection) && tryRenderSelection(selection)) {
    return;
  }

  // A corrupt BMP should not make an otherwise valid custom folder fall back
  // to the dark default screen. Re-scan only on this error path and choose
  // from the files whose headers are valid.
  if (!selection.path.empty() && selectRandomSleepImage(SleepImageMode::Custom, selection, true) &&
      tryRenderSelection(selection)) {
    return;
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true,
                  renderer.supportsAbsoluteGrayscale() &&
                      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const bool success = renderBitmapSleepScreen(bitmap);
      file.close();
      if (success) return;
    }
  }

  file.close();
  renderDefaultSleepScreen();
}

// Sleep screens preserve the inherited single HALF refresh: the original OEM
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  drawCrosscubLogo(renderer, (pageWidth - 120) / 2, (pageHeight - 120) / 2);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSSMUDGE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  const bool lightSleepScreen = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT;
  if (!lightSleepScreen) {
    renderer.invertScreen();
  }

#ifdef CROSSSMUDGE_SHOW_SLEEP_BUILD_INFO
  const std::string buildInfo = std::string(CROSSSMUDGE_BUILD_ENV) + " " + CROSSSMUDGE_VERSION;
  const std::string visibleBuildInfo =
      renderer.truncatedText(SMALL_FONT_ID, buildInfo.c_str(), pageWidth - sleepBuildInfoSideMargin * 2);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 118, visibleBuildInfo.c_str(), lightSleepScreen);
#endif

  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

bool SleepActivity::renderBitmapSleepScreen(Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  // Keep error diffusion on the screen-sized grid. Resampling an already
  // dithered source makes the source pattern alias into regular seams.
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::FIT &&
      (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight)) {
    const float scale = std::min(static_cast<float>(pageWidth) / bitmap.getWidth(),
                                 static_cast<float>(pageHeight) / bitmap.getHeight());
    const int targetWidth = static_cast<int>(std::floor((bitmap.getWidth() - 1) * scale)) + 1;
    const int targetHeight = static_cast<int>(std::floor((bitmap.getHeight() - 1) * scale)) + 1;
    bitmap.setDitheredOutputSize(targetWidth, targetHeight);
  }

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) return false;

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (!hasGreyscale) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return true;
  }

  // Prefer the Direct waveform where the panel implements it: it folds the B/W
  // base into the grayscale pass rather than pushing a separate base refresh
  // first. Keep `absolute` on the Absolute probe alone so it matches what the
  // callers pass to SleepCoverAssets and the Bitmap dither/level mode; Direct
  // is only ever an upgrade on top of it, never a substitute.
  const bool absolute = renderer.supportsAbsoluteGrayscale();
  const bool direct = absolute && renderer.supportsDirectGrayscale();
  if (absolute) {
    if (!(direct ? renderer.displayDirectGrayscaleBase() : renderer.displayAbsoluteGrayscaleBase())) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    if (bitmap.rewindToData() != BmpReaderError::Ok) {
      LOG_ERR("SLP", "Failed to rewind sleep image");
      renderer.setRenderMode(GfxRenderer::BW);
      return false;
    }
    // Absolute white margins must be present in both complete planes.
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) {
      renderer.setRenderMode(GfxRenderer::BW);
      return false;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB)
      renderer.copyGrayscaleLsbBuffers();
    else
      renderer.copyGrayscaleMsbBuffers();
  }
  renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT):
      renderNoCoverSleepScreen = &SleepActivity::renderTransparentCustomSleepScreen;
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  const bool absolute = renderer.supportsAbsoluteGrayscale() &&
                        SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  std::string coverBmpPath = SleepCoverAssets::cachedCoverPathFor(path, cropped, absolute);
  if (coverBmpPath.empty()) {
    // Cover construction may loan the framebuffer to inflate/build scratch. Keep
    // the retained visible frame outside that loan, including failure returns.
    const bool transparent = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT;
    const bool stored = transparent && renderer.storeBwBuffer();
    if (!transparent || stored) {
      if (SleepCoverAssets::prepareFullCoverForPath(path, cropped, &renderer, absolute))
        coverBmpPath = SleepCoverAssets::cachedCoverPathFor(path, cropped, absolute);
    }
    if (stored) renderer.restoreBwBuffer(false);
  }
  if (coverBmpPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file, absolute, absolute);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT) {
        // Both pointers are borrowed only within the live file/bitmap scope.
        overlayCover = &bitmap;
        renderModernBitmapSleepScreen(bitmap);
        overlayGrayCover = nullptr;
        overlayCover = nullptr;
        file.close();
        return;
      }
      const bool success = renderBitmapSleepScreen(bitmap);
      file.close();
      if (success) return;
    }
  }

  file.close();
  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderReadingStatsSleepScreen() const {
  BookReadingStats bookStats;
  std::string bookTitle = tr(STR_READING_STATS);
  float progressPercent = -1.0f;

  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (!path.empty()) {
    const std::string recentTitle = recentTitleForPath(path);
    bookTitle = recentTitle.empty() ? filenameFromPath(path) : recentTitle;

    bookStats = loadBookStatsForPath(path);
    progressPercent = RecentBookProgress::loadPercent(recentBookForPath(path));
  }

  if (!halClock.isAvailable()) {
    const GlobalReadingStats deviceStats = GlobalReadingStats::load();
    const bool hasSyncedStats = GlobalReadingStats::hasSyncedStats();
    const GlobalReadingStats allDevicesStats =
        hasSyncedStats ? GlobalReadingStats::loadAggregated(deviceStats) : GlobalReadingStats{};
    renderNoRtcCombinedStatsPage(renderer, nullptr, bookTitle, bookStats, progressPercent, false, 0, deviceStats,
                                 hasSyncedStats ? &allDevicesStats : nullptr, false);
  } else {
    renderPerBookStatsPage(renderer, nullptr, bookTitle, bookStats, progressPercent, false, 0, false, false, false);
  }
  if (!sleepCoverFilterInvertsGeneratedScreen()) {
    renderer.invertScreen();
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderMinimalSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareMinimalCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const float progressPercent = RecentBookProgress::loadPercent(book);
  MinimalTheme theme;
  theme.drawSleepScreen(renderer, book, &bookStats, progressPercent, sleepCoverFilterInvertsGeneratedScreen());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderMinimalStatsSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareMinimalCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedMinimalCoverPathFor(path);
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  const float progressPercent = RecentBookProgress::loadPercent(book);
  MinimalTheme theme;
  theme.drawStatsSleepScreen(renderer, book, &bookStats, &globalStats, progressPercent,
                             sleepCoverFilterInvertsGeneratedScreen());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderDashboardSleepScreen() const {
  const std::string& path = currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath;
  if (path.empty()) {
    return renderDefaultSleepScreen();
  }

  RecentBook book = recentBookForPath(path);
  const std::string fallbackCoverPath = book.coverBmpPath;
  book.coverBmpPath = SleepCoverAssets::cachedDashboardCoverPathFor(path);
  if (book.coverBmpPath.empty() && SleepCoverAssets::prepareDashboardCoverForPath(path, &renderer)) {
    book.coverBmpPath = SleepCoverAssets::cachedDashboardCoverPathFor(path);
  }
  if (book.coverBmpPath.empty()) {
    book.coverBmpPath = fallbackCoverPath;
  }

  const BookReadingStats bookStats = loadBookStatsForPath(path);
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  const float progressPercent = RecentBookProgress::loadPercent(book);
  const std::string chapterTitle = loadChapterTitleForPath(path);
  DashboardTheme theme;
  theme.drawSleepScreen(renderer, book, &bookStats, &globalStats, progressPercent, chapterTitle.c_str(),
                        sleepCoverFilterInvertsGeneratedScreen());
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  if (ReaderUtils::readerDarkModeEnabled()) {
    renderer.drawImageInverted(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  } else {
    renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
}

void SleepActivity::renderOverlaySleepScreen() const {
  // Overlay pictures always use portrait orientation regardless of the reader's orientation preference.
  const auto savedOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool shouldUseReaderPageBackground = canSnapshotOverlayBackground;
  const std::string path = shouldUseReaderPageBackground
                               ? (currentBookPath.empty() ? APP_STATE.openEpubPath : currentBookPath)
                               : std::string{};

  auto renderSavedReaderPage = [&]() -> bool {
    if (path.empty()) {
      return false;
    }

    if (FsHelpers::checkFileExtension(path, ".xtc") || FsHelpers::checkFileExtension(path, ".xtch")) {
      return XtcReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    if (FsHelpers::checkFileExtension(path, ".txt")) {
      return TxtReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    if (FsHelpers::checkFileExtension(path, ".epub")) {
      return EpubReaderActivity::drawCurrentPageToBuffer(path, renderer);
    }
    return false;
  };
  const bool backgroundSupportsGrayscale =
      FsHelpers::checkFileExtension(path, ".txt") || FsHelpers::checkFileExtension(path, ".epub");
  bool backgroundWasRebuilt = false;
  bool backgroundAvailable = false;

  // Step 1: Restore the screen that was visible before the sleep popup. When
  // that snapshot is unavailable in the reader, rebuild from the saved position.
  if (overlayBackgroundBufferStored) {
    renderer.restoreBwBuffer();
    backgroundAvailable = true;
  } else if (shouldUseReaderPageBackground && !path.empty()) {
    backgroundWasRebuilt = renderSavedReaderPage();
    backgroundAvailable = backgroundWasRebuilt;

    if (!backgroundWasRebuilt) {
      LOG_DBG("SLP", "Page re-render failed, using white background");
      renderer.clearScreen();
    }
  } else {
    LOG_DBG("SLP", "No current screen snapshot available for overlay sleep screen");
    renderer.clearScreen();
  }

  // Remove the live battery strip from the preserved/reconstructed reader page so the
  // overlay sleep screen still shows chapter/progress details without the battery glance target.
  if (shouldUseReaderPageBackground && backgroundAvailable) {
    hideOverlayBatteryStrip(renderer);
  }

  // Step 2: Load the overlay image using the same selection logic as renderCustomSleepScreen.
  // BMP: white pixels are skipped (transparent via drawBitmap), black pixels composited on top.
  // PNG: pixels with alpha < 128 are skipped; opaque pixels are drawn with their grayscale value.
  auto tryDrawOverlay = [&](const std::string& filename) -> OverlayDrawResult {
    HalFile file;
    if (!Storage.openFileForRead("SLP", filename, file)) {
      if (Storage.exists(filename.c_str())) {
        LOG_ERR("SLP", "BMP overlay exists but could not be opened: %s", filename.c_str());
        return OverlayDrawResult::Failed;
      }
      LOG_DBG("SLP", "BMP overlay not found: %s", filename.c_str());
      return OverlayDrawResult::NotFound;
    }
    // Keep dithering off here: error diffusion can make nominally white
    // transparent pixels visible over the preserved reader page.
    Bitmap bitmap(file);
    const BmpReaderError parseResult = bitmap.parseHeaders();
    if (parseResult != BmpReaderError::Ok) {
      LOG_ERR("SLP", "BMP overlay header parse failed for %s: %s", filename.c_str(),
              Bitmap::errorToString(parseResult));
      file.close();
      return OverlayDrawResult::Failed;
    }

    int x, y;
    float cropX = 0, cropY = 0;
    if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
      float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
      if (ratio > screenRatio) {
        x = 0;
        y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      } else {
        x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
        y = 0;
      }
    } else {
      x = (pageWidth - bitmap.getWidth()) / 2;
      y = (pageHeight - bitmap.getHeight()) / 2;
    }

    // Draw without clearScreen so the reader page remains in the frame buffer beneath
    LOG_INF("SLP", "Drawing BMP overlay: %s", filename.c_str());
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    file.close();
    return OverlayDrawResult::Drawn;
  };

  bool pngOverlayGrayscaleDisplayed = false;
  bool pngOverlayFallbackMustUseDefault = false;
  auto tryDrawPngOverlay = [&](const std::string& filename) -> OverlayDrawResult {
    if (!Storage.exists(filename.c_str())) {
      LOG_DBG("SLP", "PNG overlay not found: %s", filename.c_str());
      return OverlayDrawResult::NotFound;
    }

    // The reader activity has already released its document/layout state, and
    // Page Overlay has captured the page framebuffer. Its active SD-font glyph
    // cache is therefore regenerable and not needed for the final sleep frame.
    // Free it before PNGdec requests its contiguous decode buffer.
    const uint32_t freeBeforeRelease = ESP.getFreeHeap();
    const uint32_t maxAllocBeforeRelease = ESP.getMaxAllocHeap();
    if (renderer.releaseSdCardFontForLowMemory(SETTINGS.getReaderFontId())) {
      LOG_DBG("SLP", "Released reader font cache for PNG overlay: free=%u->%u maxAlloc=%u->%u", freeBeforeRelease,
              ESP.getFreeHeap(), maxAllocBeforeRelease, ESP.getMaxAllocHeap());
    }

    constexpr size_t MIN_FREE_HEAP = 60 * 1024;  // PNG decoder ~42 KB + overhead
    if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
      LOG_ERR("SLP", "Not enough heap for PNG overlay decoder: %u free, need %u for %s", ESP.getFreeHeap(),
              static_cast<unsigned>(MIN_FREE_HEAP), filename.c_str());
      return OverlayDrawResult::Failed;
    }
    auto png = std::unique_ptr<PNG>(new (std::nothrow) PNG());
    if (!png) {
      LOG_ERR("SLP", "Failed to allocate PNG overlay decoder for %s", filename.c_str());
      return OverlayDrawResult::Failed;
    }

    int rc = png->open(filename.c_str(), pngSleepOpen, pngSleepClose, pngSleepRead, pngSleepSeek, pngOverlayDraw);
    if (rc != PNG_SUCCESS) {
      LOG_ERR("SLP", "PNG overlay open failed for %s: %d", filename.c_str(), rc);
      return OverlayDrawResult::Failed;
    }

    const int srcW = png->getWidth(), srcH = png->getHeight();
    float yScale = 1.0f;
    int dstW = srcW, dstH = srcH;
    if (srcW > pageWidth || srcH > pageHeight) {
      const float scaleX = (float)pageWidth / srcW, scaleY = (float)pageHeight / srcH;
      const float scale = (scaleX < scaleY) ? scaleX : scaleY;
      dstW = (int)(srcW * scale);
      dstH = (int)(srcH * scale);
      yScale = (float)dstH / srcH;
    }

    PngOverlayCtx ctx;
    ctx.renderer = &renderer;
    ctx.screenW = pageWidth;
    ctx.screenH = pageHeight;
    ctx.srcWidth = srcW;
    ctx.dstWidth = dstW;
    ctx.dstX = (pageWidth - dstW) / 2;
    ctx.dstY = (pageHeight - dstH) / 2;
    ctx.yScale = yScale;
    ctx.lastDstY = -1;
    ctx.transparentColor = -2;  // will be resolved on first draw callback (after tRNS is parsed)
    ctx.pngObj = png.get();

    LOG_INF("SLP", "Drawing PNG overlay: %s", filename.c_str());
    rc = png->decode(&ctx, 0);
    png->close();
    if (rc != PNG_SUCCESS) {
      LOG_ERR("SLP", "PNG overlay decode failed for %s: %d", filename.c_str(), rc);
      return OverlayDrawResult::Failed;
    }

    const bool absolute = renderer.supportsAbsoluteGrayscale();
    if (!absolute) return OverlayDrawResult::Drawn;
    if (!(renderer.supportsDirectGrayscale() ? renderer.displayDirectGrayscaleBase()
                                             : renderer.displayAbsoluteGrayscaleBase())) {
      return OverlayDrawResult::Drawn;
    }

    // Absolute gray planes preserve the reader page underneath transparent PNG
    // pixels while rewriting each opaque pixel at its exact four-level tone.
    bool lsbCopied = false;
    const auto grayscaleFallback = [&]() {
      renderer.setRenderMode(GfxRenderer::BW);
      if (lsbCopied) {
        renderer.clearScreen();
        pngOverlayFallbackMustUseDefault = true;
      }
      return lsbCopied ? OverlayDrawResult::Failed : OverlayDrawResult::Drawn;
    };
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      png.reset();
      png = std::unique_ptr<PNG>(new (std::nothrow) PNG());
      if (!png) {
        LOG_ERR("SLP", "Failed to allocate PNG grayscale decoder for %s", filename.c_str());
        return grayscaleFallback();
      }
      rc = png->open(filename.c_str(), pngSleepOpen, pngSleepClose, pngSleepRead, pngSleepSeek, pngOverlayDraw);
      if (rc != PNG_SUCCESS) {
        LOG_ERR("SLP", "PNG grayscale open failed for %s: %d", filename.c_str(), rc);
        return grayscaleFallback();
      }

      ctx.pngObj = png.get();
      ctx.lastDstY = -1;
      ctx.transparentColor = -2;
      renderer.setRenderMode(mode);
      rc = png->decode(&ctx, 0);
      png->close();
      if (rc != PNG_SUCCESS) {
        LOG_ERR("SLP", "PNG grayscale decode failed for %s: %d", filename.c_str(), rc);
        return grayscaleFallback();
      }
      if (mode == GfxRenderer::GRAYSCALE_LSB) {
        renderer.copyGrayscaleLsbBuffers();
        lsbCopied = true;
      } else {
        renderer.copyGrayscaleMsbBuffers();
      }
    }
    renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    renderer.setRenderMode(GfxRenderer::BW);
    pngOverlayGrayscaleDisplayed = true;
    return OverlayDrawResult::Drawn;
  };

  bool overlayDrawn = false;
  bool overlayCandidateFailed = false;
  SleepImageSelection selection;
  auto trySelectedOverlay = [&](const SleepImageSelection& image) {
    LOG_INF("SLP", "Selected overlay image: %s", image.path.c_str());
    const OverlayDrawResult result = image.isPng ? tryDrawPngOverlay(image.path) : tryDrawOverlay(image.path);
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  };

  if (selectPinnedSleepImage(SleepImageMode::Overlay, selection)) {
    trySelectedOverlay(selection);
  }
  if (!overlayDrawn && !pngOverlayFallbackMustUseDefault &&
      selectRandomSleepImage(SleepImageMode::Overlay, selection)) {
    trySelectedOverlay(selection);
  }

  // Page Overlay can mix PNGs and BMPs. PNG decoding needs a sizeable
  // temporary buffer while the reader page is still resident, so an otherwise
  // valid PNG can fail on C3 devices. Try another folder image before using
  // the root/default fallback: use BMP-only after a PNG failure, or validate
  // BMP headers after a failed BMP selection.
  if (!overlayDrawn && !pngOverlayFallbackMustUseDefault && overlayCandidateFailed && !selection.path.empty() &&
      selectRandomSleepImage(SleepImageMode::Overlay, selection,
                             /*validateBmpHeaders=*/!selection.isPng,
                             /*bmpOnly=*/selection.isPng)) {
    trySelectedOverlay(selection);
  }

  if (!overlayDrawn && !pngOverlayFallbackMustUseDefault) {
    const OverlayDrawResult result = tryDrawOverlay("/sleep.bmp");
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  }
  if (!overlayDrawn && !pngOverlayFallbackMustUseDefault) {
    const OverlayDrawResult result = tryDrawPngOverlay("/sleep.png");
    overlayDrawn = result == OverlayDrawResult::Drawn;
    overlayCandidateFailed = overlayCandidateFailed || result == OverlayDrawResult::Failed;
  }

  if (!overlayDrawn) {
    if (overlayCandidateFailed) {
      LOG_ERR("SLP", "Overlay image was found but could not be drawn; falling back to default sleep screen");
      renderer.setOrientation(savedOrientation);
      return renderDefaultSleepScreen();
    }
    if (!backgroundAvailable) {
      LOG_DBG("SLP", "No overlay image or current screen snapshot available, falling back to default sleep screen");
      renderer.setOrientation(savedOrientation);
      return renderDefaultSleepScreen();
    }
    LOG_DBG("SLP", "No overlay image found, displaying background without overlay");
  }

  renderer.setOrientation(savedOrientation);
  if (pngOverlayGrayscaleDisplayed) return;
  // The grayscale re-render has no mask for the overlay image. If an overlay was
  // drawn, keep the composited BW frame intact instead of painting page glyphs
  // over the sleep image.
  const bool shouldRunGrayscalePass = shouldUseReaderPageBackground && backgroundSupportsGrayscale && !overlayDrawn &&
                                      (backgroundWasRebuilt || (overlayBackgroundBufferStored && !path.empty()));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH, !shouldRunGrayscalePass && TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);

  if (!shouldRunGrayscalePass) {
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("SLP", "Overlay: failed to store BW buffer for grayscale pass");
    return;
  }

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderSavedReaderPage()) {
    LOG_ERR("SLP", "Overlay: failed to rebuild page for grayscale LSB pass");
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
    return;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderSavedReaderPage()) {
    LOG_ERR("SLP", "Overlay: failed to rebuild page for grayscale MSB pass");
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.restoreBwBuffer();
    return;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}

void SleepActivity::renderModernBitmapSleepScreen(Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale = (bitmap.hasGreyscale() || (preserveBackground && overlayGrayCover)) &&
                            (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                       CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  if (preserveBackground && !renderer.storeBwBuffer()) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return;
  }
  if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
    if (preserveBackground) renderer.restoreBwBuffer(false);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return;
  }
  if (preserveBackground) renderer.discardStoredBwBuffer();

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Cover + Transparent: the B/W cover is the overlay's background; the overlay renderers redraw its gray planes.
  if (overlayCover == &bitmap) {
    if (hasGreyscale && renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported()) {
      overlayGrayCover = &bitmap;
    }
    renderTransparentCustomSleepScreen();
    overlayGrayCover = nullptr;
    return;
  }

  const bool absolute = hasGreyscale && renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, hasGreyscale && !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return;
  }
  if (absolute) {
    // Preserve the owner single-pass sleep refresh; do not pre-clean to white.
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return;
  } else if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  }

  if (hasGreyscale) {
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (bitmap.rewindToData() != BmpReaderError::Ok) {
        ready = false;
        break;
      }
      if (preserveBackground) {
        if (!beginOverlayGrayPlane(renderer, overlayGrayCover, plane, absolute)) {
          ready = false;
          break;
        }
      } else {
        renderer.clearScreen(absolute ? 0xFF : 0x00);
        renderer.setRenderMode(plane);
      }
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
        ready = false;
        break;
      }
      if (plane == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (ready)
      renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    else
      LOG_ERR("SLP", "Incomplete grayscale image; keeping the current display");
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog, overlayGrayCover);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderModernBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config{};
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  // Decode can fail after writing rows. Keep only one snapshot, release it
  // before the grayscale keeper stores the completed composition.
  if (!renderer.storeBwBuffer()) return false;
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.restoreBwBuffer(false);
    return false;
  }
  renderer.discardStoredBwBuffer();
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH, TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
    return true;
  }
  if (absolute) {
    // Keep the owner's single-pass waveform policy for both modern modes.
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!beginOverlayGrayPlane(renderer, overlayGrayCover, GfxRenderer::GRAYSCALE_LSB, absolute) ||
      !converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!beginOverlayGrayPlane(renderer, overlayGrayCover, GfxRenderer::GRAYSCALE_MSB, absolute) ||
      !converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer(TURN_OFF_SCREEN_AFTER_SLEEP_REFRESH);
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  if (overlayCover) {
    // Show the plain cover.
    Bitmap& cover = *overlayCover;
    overlayCover = nullptr;
    if (cover.rewindToData() == BmpReaderError::Ok) return renderModernBitmapSleepScreen(cover);
  }
  renderDefaultSleepScreen();
}
