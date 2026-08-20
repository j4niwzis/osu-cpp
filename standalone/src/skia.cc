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
#include <skia/core/SkFontMetrics.h>
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkFontArguments.h>
#include <skia/core/SkFontStyle.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkMatrix.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPixmap.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkPathBuilder.h>
#include <skia/core/SkPoint.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkRefCnt.h>
#include <skia/core/SkSamplingOptions.h>
#include <skia/core/SkShader.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkRegion.h>
#include <skia/core/SkString.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkTypeface.h>
#include <skia/core/SkVertices.h>
#include <skia/effects/SkRuntimeEffect.h>
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/GrGLTypes.h>
#include <skia/encode/SkPngEncoder.h>
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
using ::SkFontMetrics;
using ::SkFontMgr;
using ::SkFontArguments;
using ::SkFontStyle;
using ::SkFontStyleSet;
using ::SkImage;
using ::SkImageInfo;
using ::SkIRect;
using ::SkISize;
using ::SkMipmapMode;
using ::SkPaint;
using ::SkPixmap;
using ::SkPath;
using ::SkPathBuilder;
using ::SkPoint;
using ::SkRect;
using ::SkMatrix;
using ::SkRRect;
using ::SkRuntimeEffect;
using ::SkRuntimeEffectBuilder;
using ::SkSamplingOptions;
using ::SkShader;
using ::SkStream;
using ::SkStreamAsset;
using ::SkRegion;
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
