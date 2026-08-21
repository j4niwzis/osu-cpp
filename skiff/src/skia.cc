module;

#define SK_GL 1
#include <GL/gl.h>

#include <skia/codec/SkCodec.h>
#include <skia/core/SkBitmap.h>
#include <skia/core/SkBlendMode.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkColorFilter.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkData.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkFontArguments.h>
#include <skia/core/SkFontMetrics.h>
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkFontStyle.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkMatrix.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkPathBuilder.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkPoint.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkRefCnt.h>
#include <skia/core/SkSamplingOptions.h>
#include <skia/core/SkShader.h>
// Not every Skia install lays its headers out the same way, and a gradient is
// not worth failing to build over: where the shader is not reachable the
// gradient is drawn as a stack of bands instead, which is what the screens
// were doing by hand before this.
#if __has_include(<skia/effects/SkGradientShader.h>)
#include <skia/effects/SkGradientShader.h>
#define SKIFF_GRADIENT_SHADER 1
#elif __has_include(<skia/core/SkGradientShader.h>)
#include <skia/core/SkGradientShader.h>
#define SKIFF_GRADIENT_SHADER 1
#elif __has_include(<skia/SkGradientShader.h>)
#include <skia/SkGradientShader.h>
#define SKIFF_GRADIENT_SHADER 1
#else
#define SKIFF_GRADIENT_SHADER 0
#endif
#include <skia/core/SkRegion.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkString.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>
#include <skia/effects/SkRuntimeEffect.h>
#include <skia/encode/SkPngEncoder.h>
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/GrGLTypes.h>
#include <skia/ports/SkFontMgr_data.h>
#include <skia/ports/SkFontMgr_directory.h>
#include <sksl/SkSLVersion.h>

export module skia;

export namespace skia {

template <class T> using Sp = ::sk_sp<T>;

using ::SkAlphaType;
using ::SkBitmap;
using ::SkBlendMode;
using ::SkCanvas;
using ::SkCodec;
using ::SkColor;
using ::SkColor4f;
using ::SkColorFilter;
using ::SkColorFilters;
using ::SkColorSpace;
using ::SkColorType;
using ::SkData;
using ::SkFilterMode;
using ::SkFont;
using ::SkFontArguments;
using ::SkFontHinting;
using ::SkFontMetrics;
using ::SkFontMgr;
using ::SkFontStyle;
using ::SkFontStyleSet;
using ::SkImage;
using ::SkImageInfo;
using ::SkIRect;
using ::SkISize;
using ::SkMatrix;
using ::SkMipmapMode;
using ::SkPaint;
using ::SkPath;
using ::SkPathBuilder;
using ::SkPixmap;
using ::SkPoint;
using ::SkRect;
using ::SkRegion;
using ::SkRRect;
using ::SkRuntimeEffect;
using ::SkRuntimeEffectBuilder;
using ::SkSamplingOptions;
using ::SkShader;
using ::SkStream;
using ::SkStreamAsset;
using ::SkString;
using ::SkSurface;
using ::SkSurfaceProps;
using ::SkTextEncoding;
using ::SkTileMode;

using ::SkTypeface;
using ::SkVertices;

namespace png {
using ::SkPngEncoder::Encode;
using Options = ::SkPngEncoder::Options;
} // namespace png

using ::SkImages::RasterFromBitmap;

using ::GrBackendRenderTarget;
using ::GrBackendTexture;
using ::GrDirectContext;
using ::GrGLenum;
using ::GrGLFramebufferInfo;
using ::GrGLMakeNativeInterface;
using ::GrGLuint;
using ::GrSurfaceOrigin;

using ::GrBackendRenderTargets::MakeGL;
using ::GrDirectContexts::MakeGL;
using ::skgpu::Budgeted;
using ::skgpu::Budgeted::kNo;
using ::SkSurfaces::Raster;
using ::SkSurfaces::RenderTarget;
using ::SkSurfaces::WrapBackendRenderTarget;

using ::SkFontMgr_New_Custom_Data;
using ::SkFontMgr_New_Custom_Directory;

inline constexpr SkColor colorSetARGB(uint8_t a, uint8_t r, uint8_t g,
                                      uint8_t b) noexcept {
  return (static_cast<SkColor>(a) << 24) | (static_cast<SkColor>(r) << 16) |
         (static_cast<SkColor>(g) << 8) | static_cast<SkColor>(b);
}

// A two-stop linear gradient between two points, in one draw where Skia's
// gradient shader is reachable and in bands where it is not. Here rather than
// in the painter because this is the file that knows what this Skia has.
inline void linearGradient(SkCanvas *canvas, const SkRect &rect, SkPoint from,
                           SkPoint to, SkColor start, SkColor end,
                           float alpha = 1.0f) {
  if (canvas == nullptr || rect.isEmpty()) {
    return;
  }
#if SKIFF_GRADIENT_SHADER
  const SkPoint ends[2] = {from, to};
  const SkColor stops[2] = {start, end};
  SkPaint p;
  p.setAntiAlias(true);
  p.setAlphaf(alpha);
  p.setShader(::SkGradientShader::MakeLinear(ends, stops, nullptr, 2,
                                             SkTileMode::kClamp));
  canvas->drawRect(rect, p);
#else
  constexpr int kSteps = 32;
  const float dy = to.fY - from.fY < 0.0f ? from.fY - to.fY : to.fY - from.fY;
  const float dx = to.fX - from.fX < 0.0f ? from.fX - to.fX : to.fX - from.fX;
  const bool vertical = dy >= dx;
  const auto lerp = [](std::uint32_t a, std::uint32_t b, float t) {
    return static_cast<std::uint8_t>(
        static_cast<float>(a) +
        (static_cast<float>(b) - static_cast<float>(a)) * t);
  };
  SkPaint p;
  p.setAntiAlias(false);
  for (int i = 0; i < kSteps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSteps - 1);
    p.setColor(colorSetARGB(lerp((start >> 24) & 0xffu, (end >> 24) & 0xffu, t),
                            lerp((start >> 16) & 0xffu, (end >> 16) & 0xffu, t),
                            lerp((start >> 8) & 0xffu, (end >> 8) & 0xffu, t),
                            lerp(start & 0xffu, end & 0xffu, t)));
    p.setAlphaf(p.getAlphaf() * alpha);
    const float a = static_cast<float>(i) / static_cast<float>(kSteps);
    const float b = static_cast<float>(i + 1) / static_cast<float>(kSteps);
    canvas->drawRect(
        vertical ? SkRect::MakeLTRB(rect.fLeft, rect.fTop + rect.height() * a,
                                    rect.fRight,
                                    rect.fTop + rect.height() * b + 1.0f)
                 : SkRect::MakeLTRB(rect.fLeft + rect.width() * a, rect.fTop,
                                    rect.fLeft + rect.width() * b + 1.0f,
                                    rect.fBottom),
        p);
  }
#endif
}

inline constexpr GrGLenum kGlRgba8 = GL_RGBA8;

using ::kBottomLeft_GrSurfaceOrigin;
using ::kN32_SkColorType;
using ::kOpaque_SkAlphaType;
using ::kPremul_SkAlphaType;
using ::kRGBA_8888_SkColorType;
using ::kRGBA_F32_SkColorType;
using ::kTopLeft_GrSurfaceOrigin;
using ::kUnpremul_SkAlphaType;

inline constexpr SkColor kBlack = SK_ColorBLACK;
inline constexpr SkColor kWhite = SK_ColorWHITE;
inline constexpr SkColor kLTGray = SK_ColorLTGRAY;
inline constexpr SkColor kDKGray = SK_ColorDKGRAY;
inline constexpr SkColor kGray = SK_ColorGRAY;
inline constexpr SkColor kRed = SK_ColorRED;
inline constexpr SkColor kGreen = SK_ColorGREEN;
inline constexpr SkColor kBlue = SK_ColorBLUE;
inline constexpr SkColor kYellow = SK_ColorYELLOW;
inline constexpr SkColor kCyan = SK_ColorCYAN;
inline constexpr SkColor kMagenta = SK_ColorMAGENTA;

using Style = ::SkPaint::Style;
inline constexpr Style kFillStyle = ::SkPaint::Style::kFill_Style;
inline constexpr Style kStrokeStyle = ::SkPaint::Style::kStroke_Style;

// Grid fitting is what makes small static text crisp, and what makes text
// whose size is animating jump a glyph at a time: each outline snaps to the
// pixel grid at its own threshold as the size passes through it.
inline constexpr ::SkFontHinting kNoHinting = ::SkFontHinting::kNone;
inline constexpr Style kStrokeAndFillStyle =
    ::SkPaint::Style::kStrokeAndFill_Style;

using Join = ::SkPaint::Join;
inline constexpr Join kMiterJoin = ::SkPaint::Join::kMiter_Join;
inline constexpr Join kRoundJoin = ::SkPaint::Join::kRound_Join;
inline constexpr Join kBevelJoin = ::SkPaint::Join::kBevel_Join;

using Cap = ::SkPaint::Cap;
inline constexpr Cap kButtCap = ::SkPaint::Cap::kButt_Cap;
inline constexpr Cap kRoundCap = ::SkPaint::Cap::kRound_Cap;
inline constexpr Cap kSquareCap = ::SkPaint::Cap::kSquare_Cap;

using Version = ::SkSL::Version;
inline constexpr Version kSL300 = ::SkSL::Version::k300;

} // namespace skia
