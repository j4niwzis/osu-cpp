module;

// GLFW is asked for the Vulkan half of its interface: the extensions a
// surface needs on this window system, the surface itself, and the loader.
// Nothing here links libvulkan -- glfw opens it and hands over
// vkGetInstanceProcAddr, so a machine with no driver fails at the first call
// rather than at program start.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// The allocator, which is not in Skia's public headers.
//
// A Vulkan driver hands out whole blocks of device memory and permits a few
// thousand of them, so something has to suballocate; Skia builds the library
// everyone uses for that into itself with settings of its own, and a
// Graphite context cannot be made without one -- "we cannot really expose
// this to clients in a meaningful way", says the header that declares it,
// because what it is was decided when Skia was compiled.
//
// Declared here rather than included. The header lives under Skia's src/,
// which is on the include path of a build that compiles Skia and not of one
// that found it installed, and this file has to compile either way. A
// declaration of a function that exists is enough to call it; whether the
// library really has it is what the port's own probe asks before it says a
// Skia can be used this way.
// sk_sp is declared rather than included, and that is not thrift.
//
// SkRefCnt.h declares SkSafeRef and SkSafeUnref as static inline, which
// makes them local to whatever translation unit included the header -- and a
// module unit that includes it carries its own pair. A program importing two
// such modules has two, so the ones sk_sp's own copy assignment was written
// against are visible from neither: "no matching function for call to
// SkSafeRef", with no candidates listed, in a line that had compiled for
// months. The wrapper module is where Skia is included, and this one takes
// it from there.
template <typename T> class sk_sp;

namespace skgpu {
struct VulkanBackendContext;
class VulkanMemoryAllocator;
enum class ThreadSafe : bool {
  kNo = false,
  kYes = true,
};
namespace VulkanMemoryAllocators {
sk_sp<VulkanMemoryAllocator> Make(const VulkanBackendContext &, ThreadSafe);
} // namespace VulkanMemoryAllocators
} // namespace skgpu

export module platform.vulkan;

import std;
import skia;

namespace platform::vulkan {

// What a program has to load before it can ask Vulkan anything. The loader
// answers by name; these are the ones this file calls itself, and Skia loads
// what it needs through the same proc address it is handed.
struct Api {
  PFN_vkGetInstanceProcAddr fGetInstanceProcAddr = nullptr;
  PFN_vkGetDeviceProcAddr fGetDeviceProcAddr = nullptr;

  PFN_vkCreateInstance fCreateInstance = nullptr;
  PFN_vkEnumerateInstanceLayerProperties fEnumerateLayers = nullptr;
  PFN_vkDestroyInstance fDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices fEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties fGetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties fGetQueueFamilyProperties =
      nullptr;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR fGetSurfaceSupport = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fGetSurfaceCapabilities =
      nullptr;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fGetSurfaceFormats = nullptr;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fGetSurfacePresentModes =
      nullptr;
  PFN_vkCreateDevice fCreateDevice = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties fEnumerateDeviceExtensions =
      nullptr;
  PFN_vkDestroySurfaceKHR fDestroySurface = nullptr;

  PFN_vkGetDeviceQueue fGetDeviceQueue = nullptr;
  PFN_vkDestroyDevice fDestroyDevice = nullptr;
  PFN_vkDeviceWaitIdle fDeviceWaitIdle = nullptr;
  PFN_vkCreateSwapchainKHR fCreateSwapchain = nullptr;
  PFN_vkDestroySwapchainKHR fDestroySwapchain = nullptr;
  PFN_vkGetSwapchainImagesKHR fGetSwapchainImages = nullptr;
  PFN_vkAcquireNextImageKHR fAcquireNextImage = nullptr;
  PFN_vkQueuePresentKHR fQueuePresent = nullptr;
  PFN_vkCreateSemaphore fCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore fDestroySemaphore = nullptr;

  template <class Fn>
  static Fn instanceProc(VkInstance instance, const char *name) {
    return reinterpret_cast<Fn>(::glfwGetInstanceProcAddress(instance, name));
  }
};

// One acquire and one render-finished semaphore, and whether the work they
// belong to has been reported finished. A frame cannot use a slot again
// until the GPU is done with it: the semaphores are still in flight.
struct Slot {
  VkSemaphore fAcquired = VK_NULL_HANDLE;
  VkSemaphore fRendered = VK_NULL_HANDLE;
  bool fInFlight = false;
};

// The images this program draws, as images the recorder can draw.
//
// Graphite draws its own and nothing else. Everything this client hands it --
// a decoded beatmap background, a skin texture, a slider body built into a
// surface -- was made somewhere else, and without one of these each of them
// is dropped where it is drawn, with a line on the console per attempt:
// "Couldn't convert SkImage to a Graphite-backed representation".
//
// Converting is an upload, so the results are kept: the same texture is
// drawn every frame, and uploading it again per frame is the difference
// between a game and a slideshow. Keyed by the image's own identity and by
// whether mip levels were asked for, since the same picture at two
// requirements is two textures.
//
// The cache is dropped whole when it grows past what a skin and a library of
// covers can account for. Nothing here knows when a texture stops being
// used, and a cache that only grows is a phone that runs out of memory.
class Images final : public skia::graphite::ImageProvider {
public:
  skia::Sp<skia::SkImage>
  findOrCreate(skia::graphite::Recorder *recorder, const skia::SkImage *image,
               skia::SkImage::RequiredProperties required) override {
    if (recorder == nullptr || image == nullptr) {
      return nullptr;
    }
    const Key key{image->uniqueID(), required.fMipmapped};
    if (const auto found = fCache.find(key); found != fCache.end()) {
      return found->second;
    }
    if (fCache.size() > kKeep) {
      fCache.clear();
    }
    skia::Sp<skia::SkImage> texture =
        skia::graphite::TextureFromImage(recorder, image, required);
    if (texture) {
      fCache.emplace(key, texture);
    }
    return texture;
  }

private:
  struct Key {
    std::uint32_t fId = 0;
    bool fMipmapped = false;
    bool operator==(const Key &) const = default;
  };
  struct Hash {
    std::size_t operator()(const Key &key) const noexcept {
      return std::hash<std::uint64_t>{}(
          (static_cast<std::uint64_t>(key.fId) << 1) |
          static_cast<std::uint64_t>(key.fMipmapped));
    }
  };
  static constexpr std::size_t kKeep = 512;
  std::unordered_map<Key, skia::Sp<skia::SkImage>, Hash> fCache;
};

} // namespace platform::vulkan

// The drivers this program was built with, where it was built with any.
//
// A Vulkan program normally finds its driver through the loader: a shared
// object that reads a directory of manifests, opens whichever driver each
// names, and merges them into one API. A program that opens nothing has no
// loader and no manifests -- it has the drivers themselves, linked in, each
// with the entry point every driver has under a name of its own.
//
// So this is the loader, in the only part of it a program actually needs:
// ask each driver whether it has a device, and take the first that does.
// One device is what a game uses; enumerating all of them across drivers is
// what a loader does for programs that choose, and this one does not.
#if defined(OSU_STATIC_DRIVERS)
extern "C" {
#define OSU_VULKAN_DRIVER(name)                                                \
  PFN_vkVoidFunction osu_vulkan_driver_##name(VkInstance instance,             \
                                              const char *symbol);
#include "osu_vulkan_drivers.h"
#undef OSU_VULKAN_DRIVER
}

namespace {

struct Driver {
  const char *fName;
  PFN_vkGetInstanceProcAddr fProc;
};

const Driver *drivers(std::size_t *count) {
  static const Driver known[] = {
#define OSU_VULKAN_DRIVER(name) {#name, &osu_vulkan_driver_##name},
#include "osu_vulkan_drivers.h"
#undef OSU_VULKAN_DRIVER
  };
  *count = sizeof(known) / sizeof(known[0]);
  return known;
}

// Whether this driver has anything to draw on.
//
// The only way to ask is to make an instance and count the devices, which
// is what the loader does too. The instance is thrown away: what is kept is
// the answer, and the entry point that gave it.
bool driverHasDevice(const Driver &driver) {
  auto create = reinterpret_cast<PFN_vkCreateInstance>(
      driver.fProc(nullptr, "vkCreateInstance"));
  if (create == nullptr) {
    return false;
  }
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pApplicationInfo = &application;
  VkInstance instance = VK_NULL_HANDLE;
  if (create(&info, nullptr, &instance) != VK_SUCCESS) {
    return false;
  }
  auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
      driver.fProc(instance, "vkEnumeratePhysicalDevices"));
  auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(
      driver.fProc(instance, "vkDestroyInstance"));
  std::uint32_t devices = 0;
  if (enumerate != nullptr) {
    enumerate(instance, &devices, nullptr);
  }
  if (destroy != nullptr) {
    destroy(instance, nullptr);
  }
  return devices > 0;
}

} // namespace
#endif

export namespace platform::vulkan {

// Whether this build has the backend compiled into it at all. Whether the
// machine has a driver is a different question, asked of glfw once it is
// initialised -- which is after the window system has been started, not
// before, since glfw answers "no" to everything until then.
bool supported() { return true; }

// Which driver draws, decided before anything is opened.
//
// glfw is told, because everything this client asks Vulkan goes through the
// proc address glfw hands out -- and glfw's own way of getting one is to
// open the loader, which is exactly what this build has none of. Called
// before glfwInit, which is where glfw looks.
//
// Nothing to decide where the drivers are not linked in: there the loader
// on the machine is what answers, and it is right to let it.
void installLoader() {
#if defined(OSU_STATIC_DRIVERS)
  std::size_t count = 0;
  const Driver *known = drivers(&count);
  for (std::size_t index = 0; index < count; ++index) {
    if (!driverHasDevice(known[index])) {
      continue;
    }
    std::println(std::cerr, "[vulkan] driver: {}", known[index].fName);
    ::glfwInitVulkanLoader(known[index].fProc);
    return;
  }
  std::println(std::cerr,
               "[vulkan] none of the drivers built in has a device here");
#endif
}

// The window system's side of a Graphite program: a device, a swapchain, and
// a surface per frame that Skia draws into.
//
// Graphite does not talk to a window. It records drawing and plays it into
// textures; which texture a frame lands in, and how that texture reaches the
// screen, is this program's business. So each frame acquires an image from
// the swapchain, wraps it as a Skia surface, and after the recording is
// submitted the image is presented.
class Presenter {
public:
  Presenter() = default;
  Presenter(const Presenter &) = delete;
  Presenter &operator=(const Presenter &) = delete;
  ~Presenter() { this->stop(); }

  // Says what went wrong, once, where a person can read it. Every failure
  // here ends with the client falling back to the other renderer, so the
  // reason has to be printed rather than returned into a boolean.
  static void say(std::string_view what) {
    std::println(std::cerr, "[vulkan] {}", what);
  }

  // Whether frames wait for the display. The client has a setting for it and
  // this is the same question: with it on the swapchain is FIFO, which is
  // the display's own pace; with it off the frame is shown as soon as it is
  // ready. Changing it remakes the swapchain, since a present mode is
  // decided when one is made.
  void setWaitForDisplay(bool wait) {
    if (wait == fWaitForDisplay) {
      return;
    }
    fWaitForDisplay = wait;
    fSwapchainStale = true;
  }

  [[nodiscard]] bool start(::GLFWwindow *window, int width, int height) {
    fWindow = window;
    if (::glfwVulkanSupported() != GLFW_TRUE) {
      say("glfw found no vulkan loader on this machine");
      return false;
    }
    fApi.fGetInstanceProcAddr =
        Api::instanceProc<PFN_vkGetInstanceProcAddr>(nullptr,
                                                     "vkGetInstanceProcAddr");
    fApi.fCreateInstance =
        Api::instanceProc<PFN_vkCreateInstance>(nullptr, "vkCreateInstance");
    fApi.fEnumerateLayers =
        Api::instanceProc<PFN_vkEnumerateInstanceLayerProperties>(
            nullptr, "vkEnumerateInstanceLayerProperties");
    if (!fApi.fGetInstanceProcAddr || !fApi.fCreateInstance) {
      say("the loader has no vkCreateInstance");
      return false;
    }
    if (!this->makeInstance() || !this->makeSurface() ||
        !this->makeDevice() || !this->makeContext()) {
      return false;
    }
    // And a swapchain, here rather than on the first frame.
    //
    // Everything above can succeed on a machine where images cannot reach
    // the screen at all -- an X server with no DRI3, a display the driver
    // will not present to -- and finding that out on the first frame is
    // finding it out after the window has been made without a graphics
    // context, which is too late to draw any other way. Made now, a refusal
    // is what it should be: this backend saying it cannot run here, in time
    // for the client to make an ordinary window and draw through GL.
    //
    // At the size the window is. A surface may leave the size to the window
    // -- Wayland does -- and then asking it for its own says nothing, which
    // is a swapchain of one pixel by one and a remake on the first frame.
    if (!this->makeSwapchain(width, height)) {
      return false;
    }
    return true;
  }

  void stop() {
    if (fDevice != VK_NULL_HANDLE && fApi.fDeviceWaitIdle) {
      fApi.fDeviceWaitIdle(fDevice);
    }
    // In the order things belong to each other. A surface wrapping a
    // swapchain image belongs to the recorder that made it, the images it
    // converted belong there too, and both belong to the context: dropping
    // the context first leaves them referring to a device that is gone, and
    // what follows is a hang and then a fault in whoever unrefs them next.
    fSurfaces.clear();
    fImageProvider.reset();
    fRecorder.reset();
    fContext.reset();
    this->dropSwapchain();
    for (Slot &slot : fSlots) {
      if (slot.fAcquired != VK_NULL_HANDLE) {
        fApi.fDestroySemaphore(fDevice, slot.fAcquired, nullptr);
      }
      if (slot.fRendered != VK_NULL_HANDLE) {
        fApi.fDestroySemaphore(fDevice, slot.fRendered, nullptr);
      }
    }
    fSlots.clear();
    if (fDevice != VK_NULL_HANDLE) {
      fApi.fDestroyDevice(fDevice, nullptr);
      fDevice = VK_NULL_HANDLE;
    }
    if (fSurface != VK_NULL_HANDLE) {
      fApi.fDestroySurface(fInstance, fSurface, nullptr);
      fSurface = VK_NULL_HANDLE;
    }
    if (fInstance != VK_NULL_HANDLE) {
      fApi.fDestroyInstance(fInstance, nullptr);
      fInstance = VK_NULL_HANDLE;
    }
  }

  // Whether there is a device behind this. Everything below answers with
  // nothing when there is not, so that a caller which asked for Vulkan and
  // did not get it goes on drawing the way it always did.
  [[nodiscard]] bool running() const { return fContext && fRecorder; }

  // An offscreen surface, which is what the cached subtrees and anything
  // else that draws aside asks for. Made on the recorder: with Graphite a
  // surface belongs to the recorder that will record the drawing, not to the
  // context that plays it.
  [[nodiscard]] skia::Sp<skia::SkSurface> offscreen(int width, int height) {
    if (!this->running() || width <= 0 || height <= 0) {
      return nullptr;
    }
    return skia::RenderTarget(
        fRecorder.get(),
        skia::SkImageInfo::Make(width, height, skia::kRGBA_8888_SkColorType,
                                skia::kPremul_SkAlphaType));
  }

  // Nothing, and deliberately so: a Ganesh caller submits a surface when it
  // has finished drawing into it, and here the recorder already took the
  // calls -- they are played when the frame is. Kept as a call so that a
  // caller does not have to know which of the two it is talking to.
  void finish(skia::SkSurface *) {}

  // The surface this frame is drawn into, or nothing if the swapchain has to
  // be rebuilt and the frame should be skipped. The size is the window's, in
  // pixels: a swapchain is made for a size and stops matching when the
  // window changes.
  [[nodiscard]] skia::Sp<skia::SkSurface> beginFrame(int width, int height) {
    if (width <= 0 || height <= 0) {
      return nullptr;
    }
    if (fSwapchain == VK_NULL_HANDLE || width != fWidth || height != fHeight) {
      // A swapchain that could not be made for this size will not be made
      // for it on the next frame either, and a reason printed sixty times a
      // second is a reason nobody can read. Tried again when the size
      // changes, which is when the answer can change.
      if (fFailedWidth == width && fFailedHeight == height) {
        return nullptr;
      }
      if (!this->makeSwapchain(width, height)) {
        fFailedWidth = width;
        fFailedHeight = height;
        return nullptr;
      }
      fFailedWidth = 0;
      fFailedHeight = 0;
    }
    // A slot the GPU has finished with. Where none is free the work already
    // submitted is waited for, which is the honest way to bound how far
    // ahead this can run.
    fSlot = this->freeSlot();
    if (fSlot < 0) {
      fContext->submit(skia::graphite::SyncToCpu::kYes);
      fSlot = this->freeSlot();
      if (fSlot < 0) {
        say("no frame slot came free");
        return nullptr;
      }
    }
    const auto beforeAcquire = std::chrono::steady_clock::now();
    const VkResult acquired = fApi.fAcquireNextImage(
        fDevice, fSwapchain, std::numeric_limits<std::uint64_t>::max(),
        fSlots[static_cast<std::size_t>(fSlot)].fAcquired, VK_NULL_HANDLE,
        &fImage);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR ||
        acquired == VK_SUBOPTIMAL_KHR) {
      // The window changed under us: the swapchain is remade on the next
      // frame rather than mid-way through this one.
      fSwapchainStale = true;
      return nullptr;
    }
    if (acquired != VK_SUCCESS) {
      say("the swapchain would not hand over an image");
      return nullptr;
    }
    const auto now = std::chrono::steady_clock::now();
    fAcquireUs =
        std::chrono::duration_cast<std::chrono::microseconds>(now - beforeAcquire)
            .count();
    // Where the frame's drawing begins, so that what the client spends
    // between here and the recording is accounted for too.
    fDrawFrom = now;
    return fSurfaces[fImage];
  }

  // What the recording is played into, and then presented. Returns false
  // where the frame did not reach the screen, which the client reports the
  // same way it reports a failed swap.
  [[nodiscard]] bool endFrame(skia::SkSurface *surface) {
    if (fSlot < 0 || surface == nullptr) {
      return false;
    }
    Slot &slot = fSlots[static_cast<std::size_t>(fSlot)];
    const auto beforeSnap = std::chrono::steady_clock::now();
    std::unique_ptr<skia::graphite::Recording> recording = fRecorder->snap();
    const auto afterSnap = std::chrono::steady_clock::now();
    if (!recording) {
      say("the recorder had nothing to play");
      return false;
    }
    // The image has to be handed to the window system in the layout it
    // expects, and Skia is the one that knows what layout it left it in --
    // so it is told what to leave it in instead.
    skia::graphite::MutableTextureState present =
        skia::graphite::mutableTextureStates::MakeVulkan(
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, fQueueFamily);
    skia::graphite::BackendSemaphore wait =
        skia::graphite::backendSemaphores::MakeVulkan(slot.fAcquired);
    skia::graphite::BackendSemaphore signal =
        skia::graphite::backendSemaphores::MakeVulkan(slot.fRendered);
    skia::graphite::InsertRecordingInfo info;
    info.fRecording = recording.get();
    info.fTargetSurface = surface;
    info.fTargetTextureState = &present;
    info.fNumWaitSemaphores = 1;
    info.fWaitSemaphores = &wait;
    info.fNumSignalSemaphores = 1;
    info.fSignalSemaphores = &signal;
    slot.fInFlight = true;
    info.fFinishedContext = &slot;
    info.fFinishedProc = [](void *context, skia::graphite::CallbackResult) {
      static_cast<Slot *>(context)->fInFlight = false;
    };
    if (fContext->insertRecording(info) !=
        skia::graphite::InsertStatus::kSuccess) {
      slot.fInFlight = false;
      say("the recording was refused");
      return false;
    }
    if (!fContext->submit()) {
      say("the context would not submit");
      return false;
    }
    const auto afterSubmit = std::chrono::steady_clock::now();

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &slot.fRendered;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &fSwapchain;
    present_info.pImageIndices = &fImage;
    const VkResult presented = fApi.fQueuePresent(fQueue, &present_info);
    const auto afterPresent = std::chrono::steady_clock::now();
    // This image now holds this frame, which is what makes it n frames old
    // when it comes back around.
    ++fFrames;
    if (fImage < fShownAt.size()) {
      fShownAt[fImage] = fFrames;
    }
    fSlot = -1;

    // Where a frame went, for whoever is asking. Off unless
    // OSU_VULKAN_TIMING is set, because it is four numbers a frame and the
    // only reason to want them is that something is slow.
    if (fTiming) {
      const auto us = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from)
            .count();
      };
      fDrawUs += us(fDrawFrom, beforeSnap);
      fSnapUs += us(beforeSnap, afterSnap);
      fSubmitUs += us(afterSnap, afterSubmit);
      fPresentUs += us(afterSubmit, afterPresent);
      fWaitUs += fAcquireUs;
      if (++fTimed >= 60) {
        std::println(std::cerr,
                     "[vulkan] per frame over {} frames: acquire {} us, "
                     "draw {} us, record {} us, submit {} us, present {} us",
                     fTimed, fWaitUs / fTimed, fDrawUs / fTimed,
                     fSnapUs / fTimed, fSubmitUs / fTimed,
                     fPresentUs / fTimed);
        fTimed = 0;
        fSnapUs = fSubmitUs = fPresentUs = fWaitUs = fDrawUs = 0;
      }
    }
    if (presented == VK_ERROR_OUT_OF_DATE_KHR ||
        presented == VK_SUBOPTIMAL_KHR) {
      fSwapchainStale = true;
      return true;
    }
    if (presented != VK_SUCCESS) {
      say("the image was not presented");
      return false;
    }
    return true;
  }

  // How old the contents of the image this frame draws into are, in frames.
  //
  // The same question EGL_EXT_buffer_age answers for a GL client, asked of a
  // swapchain: zero means what is in the image is not this client's -- it has
  // never been drawn, or the swapchain was remade -- and n means it holds the
  // frame from n frames ago. What the client does with that is repaint only
  // what has changed since.
  [[nodiscard]] int bufferAge() const {
    if (fSlot < 0 || fImage >= fShownAt.size() || fShownAt[fImage] == 0) {
      return 0;
    }
    // Counting from the frame being drawn, which is the one after the last
    // one presented. An image carrying the frame immediately before this one
    // is one frame old, not none -- and none is what says "nothing of ours
    // is in here". Off by one the other way, the client repaints the union
    // of one frame too few, and what is left is the picture from before it.
    return static_cast<int>(fFrames + 1 - fShownAt[fImage]);
  }

  // Whether the swapchain has been told it no longer matches the window.
  // Read and cleared by the client, which remakes it on the next frame.
  [[nodiscard]] bool takeStale() {
    const bool stale = fSwapchainStale;
    fSwapchainStale = false;
    return stale;
  }

private:
  [[nodiscard]] int freeSlot() const {
    for (std::size_t i = 0; i < fSlots.size(); ++i) {
      if (!fSlots[i].fInFlight) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  [[nodiscard]] bool makeInstance() {
    std::uint32_t count = 0;
    const char **extensions = ::glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr) {
      say("glfw cannot say which extensions a surface needs here");
      return false;
    }
    std::vector<const char *> wanted(extensions, extensions + count);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "osu-cpp";
    // Skia asks for 1.1 as its minimum and reads this back out of the
    // context, so what is asked for here is what it is told there.
    app.apiVersion = VK_API_VERSION_1_1;

    // The validation layer, when it is asked for and installed. It is what
    // turns "the driver refused" into a sentence about which rule was
    // broken, and it costs enough per call that it is not something to have
    // on by default: OSU_VULKAN_VALIDATION=1 in the environment says so for
    // one run.
    std::vector<const char *> layers;
    if (std::getenv("OSU_VULKAN_VALIDATION") != nullptr &&
        fApi.fEnumerateLayers != nullptr) {
      std::uint32_t count = 0;
      fApi.fEnumerateLayers(&count, nullptr);
      std::vector<VkLayerProperties> installed(count);
      if (count > 0) {
        fApi.fEnumerateLayers(&count, installed.data());
      }
      for (const VkLayerProperties &one : installed) {
        if (std::string_view(one.layerName) == "VK_LAYER_KHRONOS_validation") {
          layers.push_back("VK_LAYER_KHRONOS_validation");
          say("the validation layer is on for this run");
          break;
        }
      }
      if (layers.empty()) {
        say("validation was asked for and VK_LAYER_KHRONOS_validation is not "
            "installed");
      }
    }

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = static_cast<std::uint32_t>(wanted.size());
    create.ppEnabledExtensionNames = wanted.data();
    create.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create.ppEnabledLayerNames = layers.data();
    if (fApi.fCreateInstance(&create, nullptr, &fInstance) != VK_SUCCESS) {
      say("no vulkan instance");
      return false;
    }
    fInstanceExtensions = std::move(wanted);
    return this->loadInstanceApi();
  }

  [[nodiscard]] bool loadInstanceApi() {
    auto proc = [this](const char *name) {
      return ::glfwGetInstanceProcAddress(fInstance, name);
    };
    fApi.fGetDeviceProcAddr =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(proc("vkGetDeviceProcAddr"));
    fApi.fDestroyInstance =
        reinterpret_cast<PFN_vkDestroyInstance>(proc("vkDestroyInstance"));
    fApi.fEnumeratePhysicalDevices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            proc("vkEnumeratePhysicalDevices"));
    fApi.fGetPhysicalDeviceProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            proc("vkGetPhysicalDeviceProperties"));
    fApi.fGetQueueFamilyProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            proc("vkGetPhysicalDeviceQueueFamilyProperties"));
    fApi.fGetSurfaceSupport =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            proc("vkGetPhysicalDeviceSurfaceSupportKHR"));
    fApi.fGetSurfaceCapabilities =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            proc("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    fApi.fGetSurfaceFormats =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            proc("vkGetPhysicalDeviceSurfaceFormatsKHR"));
    fApi.fGetSurfacePresentModes =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
            proc("vkGetPhysicalDeviceSurfacePresentModesKHR"));
    fApi.fCreateDevice =
        reinterpret_cast<PFN_vkCreateDevice>(proc("vkCreateDevice"));
    fApi.fEnumerateDeviceExtensions =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            proc("vkEnumerateDeviceExtensionProperties"));
    fApi.fDestroySurface =
        reinterpret_cast<PFN_vkDestroySurfaceKHR>(proc("vkDestroySurfaceKHR"));
    const bool complete =
        fApi.fGetDeviceProcAddr && fApi.fDestroyInstance &&
        fApi.fEnumeratePhysicalDevices && fApi.fGetPhysicalDeviceProperties &&
        fApi.fGetQueueFamilyProperties && fApi.fGetSurfaceSupport &&
        fApi.fGetSurfaceCapabilities && fApi.fGetSurfaceFormats &&
        fApi.fGetSurfacePresentModes && fApi.fCreateDevice &&
        fApi.fEnumerateDeviceExtensions && fApi.fDestroySurface;
    if (!complete) {
      say("the instance is missing calls this needs");
    }
    return complete;
  }

  [[nodiscard]] bool makeSurface() {
    if (::glfwCreateWindowSurface(fInstance, fWindow, nullptr, &fSurface) !=
        VK_SUCCESS) {
      say("no surface for this window");
      return false;
    }
    return true;
  }

  [[nodiscard]] bool makeDevice() {
    std::uint32_t count = 0;
    fApi.fEnumeratePhysicalDevices(fInstance, &count, nullptr);
    if (count == 0) {
      say("this machine reports no vulkan device");
      return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    fApi.fEnumeratePhysicalDevices(fInstance, &count, devices.data());

    // A device with a queue that can both draw and present to this surface.
    // Discrete first, because a machine with two of them means the other one
    // for a game.
    int best = -1;
    std::uint32_t bestFamily = 0;
    int bestRank = -1;
    for (std::size_t i = 0; i < devices.size(); ++i) {
      std::uint32_t families = 0;
      fApi.fGetQueueFamilyProperties(devices[i], &families, nullptr);
      std::vector<VkQueueFamilyProperties> properties(families);
      fApi.fGetQueueFamilyProperties(devices[i], &families, properties.data());
      for (std::uint32_t family = 0; family < families; ++family) {
        if ((properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
          continue;
        }
        VkBool32 presents = VK_FALSE;
        fApi.fGetSurfaceSupport(devices[i], family, fSurface, &presents);
        if (presents != VK_TRUE) {
          continue;
        }
        VkPhysicalDeviceProperties about{};
        fApi.fGetPhysicalDeviceProperties(devices[i], &about);
        const int rank =
            about.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU    ? 3
            : about.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
            : about.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 1
                                                                        : 0;
        if (rank > bestRank) {
          bestRank = rank;
          best = static_cast<int>(i);
          bestFamily = family;
          fDeviceName = about.deviceName;
        }
        break;
      }
    }
    if (best < 0) {
      say("no device can both draw and present to this window");
      return false;
    }
    fPhysical = devices[static_cast<std::size_t>(best)];
    fQueueFamily = bestFamily;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = fQueueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    // What the device is asked for: the swapchain, without which there is
    // nothing to present to, and the one that lets Skia know which driver
    // this is. Skia says so itself when it is missing -- "driver workarounds
    // cannot be correctly applied" -- and a workaround not applied on a
    // driver that needs it is a wrong picture rather than an error. It is
    // core from Vulkan 1.2 and this asks for 1.1, so it is asked for by name
    // where the device has it.
    std::uint32_t available = 0;
    fApi.fEnumerateDeviceExtensions(fPhysical, nullptr, &available, nullptr);
    std::vector<VkExtensionProperties> properties(available);
    if (available > 0) {
      fApi.fEnumerateDeviceExtensions(fPhysical, nullptr, &available,
                                      properties.data());
    }
    const auto has = [&properties](const char *name) {
      for (const VkExtensionProperties &one : properties) {
        if (std::string_view(one.extensionName) == name) {
          return true;
        }
      }
      return false;
    };
    std::vector<const char *> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (has(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
      extensions.push_back(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    }
    VkDeviceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;
    create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    if (fApi.fCreateDevice(fPhysical, &create, nullptr, &fDevice) !=
        VK_SUCCESS) {
      say("the device would not be created");
      return false;
    }
    fDeviceExtensions = extensions;
    return this->loadDeviceApi();
  }

  [[nodiscard]] bool loadDeviceApi() {
    auto proc = [this](const char *name) {
      return fApi.fGetDeviceProcAddr(fDevice, name);
    };
    fApi.fGetDeviceQueue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(proc("vkGetDeviceQueue"));
    fApi.fDestroyDevice =
        reinterpret_cast<PFN_vkDestroyDevice>(proc("vkDestroyDevice"));
    fApi.fDeviceWaitIdle =
        reinterpret_cast<PFN_vkDeviceWaitIdle>(proc("vkDeviceWaitIdle"));
    fApi.fCreateSwapchain =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(proc("vkCreateSwapchainKHR"));
    fApi.fDestroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        proc("vkDestroySwapchainKHR"));
    fApi.fGetSwapchainImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        proc("vkGetSwapchainImagesKHR"));
    fApi.fAcquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        proc("vkAcquireNextImageKHR"));
    fApi.fQueuePresent =
        reinterpret_cast<PFN_vkQueuePresentKHR>(proc("vkQueuePresentKHR"));
    fApi.fCreateSemaphore =
        reinterpret_cast<PFN_vkCreateSemaphore>(proc("vkCreateSemaphore"));
    fApi.fDestroySemaphore =
        reinterpret_cast<PFN_vkDestroySemaphore>(proc("vkDestroySemaphore"));
    const bool complete =
        fApi.fGetDeviceQueue && fApi.fDestroyDevice && fApi.fDeviceWaitIdle &&
        fApi.fCreateSwapchain && fApi.fDestroySwapchain &&
        fApi.fGetSwapchainImages && fApi.fAcquireNextImage &&
        fApi.fQueuePresent && fApi.fCreateSemaphore && fApi.fDestroySemaphore;
    if (!complete) {
      say("the device is missing calls this needs");
      return false;
    }
    fApi.fGetDeviceQueue(fDevice, fQueueFamily, 0, &fQueue);
    return fQueue != VK_NULL_HANDLE;
  }

  [[nodiscard]] bool makeContext() {
    const skia::graphite::VulkanGetProc lookup =
        [this](const char *name, VkInstance instance,
               VkDevice device) -> PFN_vkVoidFunction {
      return device != VK_NULL_HANDLE ? fApi.fGetDeviceProcAddr(device, name)
                                      : fApi.fGetInstanceProcAddr(instance,
                                                                  name);
    };
    fExtensions.init(
        lookup, fInstance, fPhysical,
        static_cast<std::uint32_t>(fInstanceExtensions.size()),
        fInstanceExtensions.data(),
        static_cast<std::uint32_t>(fDeviceExtensions.size()),
        fDeviceExtensions.data());

    skia::graphite::VulkanBackendContext backend;
    backend.fInstance = fInstance;
    backend.fPhysicalDevice = fPhysical;
    backend.fDevice = fDevice;
    backend.fQueue = fQueue;
    backend.fGraphicsQueueIndex = fQueueFamily;
    backend.fMaxAPIVersion = VK_API_VERSION_1_1;
    backend.fVkExtensions = &fExtensions;
    backend.fGetProc = lookup;
    // Skia will not make one of these for itself: the allocator it builds is
    // not something its public headers offer, and a context with none is
    // refused.
    backend.fMemoryAllocator = ::skgpu::VulkanMemoryAllocators::Make(
        backend, ::skgpu::ThreadSafe::kNo);
    if (!backend.fMemoryAllocator) {
      say("no memory allocator for this device");
      return false;
    }

    skia::graphite::ContextOptions options;
    fContext = skia::graphite::contextFactory::MakeVulkan(backend, options);
    if (!fContext) {
      say("graphite would not take this device");
      return false;
    }
    // The converter belongs to the recorder: it is asked for every image
    // that was not made by this backend, and without it each of those is
    // dropped where it is drawn.
    fImageProvider = skia::Sp<Images>(new Images());
    skia::graphite::RecorderOptions recording;
    recording.fImageProvider = fImageProvider;
    fRecorder = fContext->makeRecorder(recording);
    if (!fRecorder) {
      say("no recorder");
      return false;
    }
    std::println(std::cerr, "[vulkan] drawing on {}", fDeviceName);
    return true;
  }

  void dropSwapchain() {
    if (fDevice != VK_NULL_HANDLE && fApi.fDeviceWaitIdle) {
      fApi.fDeviceWaitIdle(fDevice);
    }
    fSurfaces.clear();
    fImages.clear();
    if (fSwapchain != VK_NULL_HANDLE) {
      fApi.fDestroySwapchain(fDevice, fSwapchain, nullptr);
      fSwapchain = VK_NULL_HANDLE;
    }
  }

  [[nodiscard]] bool makeSwapchain(int width, int height) {
    this->dropSwapchain();

    VkSurfaceCapabilitiesKHR capabilities{};
    if (fApi.fGetSurfaceCapabilities(fPhysical, fSurface, &capabilities) !=
        VK_SUCCESS) {
      say("the surface would not say what it can do");
      return false;
    }

    std::uint32_t formats = 0;
    fApi.fGetSurfaceFormats(fPhysical, fSurface, &formats, nullptr);
    if (formats == 0) {
      say("the surface offers no format");
      return false;
    }
    std::vector<VkSurfaceFormatKHR> available(formats);
    fApi.fGetSurfaceFormats(fPhysical, fSurface, &formats, available.data());
    // The one this client already draws in. A surface that cannot offer it is
    // not a surface this can wrap without converting every frame.
    VkSurfaceFormatKHR chosen = available.front();
    bool found = false;
    for (const VkSurfaceFormatKHR &format : available) {
      if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
          format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = format;
        found = true;
        break;
      }
    }
    if (!found) {
      for (const VkSurfaceFormatKHR &format : available) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
          chosen = format;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      say("the surface offers neither RGBA8 nor BGRA8");
      return false;
    }

    // The size the surface says, and the window's own where it says the
    // window decides -- clamped either way, because a driver refuses an
    // extent outside what it reported and says nothing about which part of
    // it was wrong.
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
      extent.width = static_cast<std::uint32_t>(std::max(0, width));
      extent.height = static_cast<std::uint32_t>(std::max(0, height));
    }
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height =
        std::clamp(extent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0) {
      // A minimised window, which is not a failure and not a swapchain
      // either.
      return false;
    }

    // Two more than the driver's minimum. One more is the usual advice for
    // waiting on the display; where frames are shown as soon as they are
    // ready, the client runs ahead of the compositor and waits for an image
    // to come back, which is what the time in acquire is.
    std::uint32_t images =
        capabilities.minImageCount + (fWaitForDisplay ? 1u : 2u);
    // Both of these are worth measuring on a machine rather than deciding
    // here: how deep the queue of images is and how they reach the screen
    // decide how long a frame waits for one, and what is best depends on the
    // compositor as much as on the driver. OSU_VULKAN_IMAGES and
    // OSU_VULKAN_PRESENT say so for one run.
    if (const char *said = std::getenv("OSU_VULKAN_IMAGES")) {
      const int asked = std::atoi(said);
      if (asked > 0) {
        images = static_cast<std::uint32_t>(asked);
      }
    }
    if (capabilities.maxImageCount != 0 &&
        images > capabilities.maxImageCount) {
      images = capabilities.maxImageCount;
    }

    // What a surface will actually give. Every one of these is a set of bits
    // the surface reported, and asking for a bit outside the set is refused
    // as a whole -- so what is asked for is the intersection of what this
    // needs and what was offered.
    //
    // Drawn into, and sampled from: Graphite has one way to be handed an
    // image somebody else owns, and that is as a texture, so an image that
    // cannot be sampled cannot be drawn into by it at all. Skia's own window
    // context asks for the same set and falls back to wrapping a render
    // target where sampling is missing -- a thing Graphite does not have.
    //
    // The rest are taken where they are offered: copying in and out is how a
    // driver clears or reads a whole image, and an input attachment is how
    // this backend reads the destination while blending.
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((capabilities.supportedUsageFlags &
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
      say("the surface will not be drawn into, which is all this wanted");
      return false;
    }
    for (const VkImageUsageFlags optional :
         {static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
          static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_TRANSFER_DST_BIT),
          static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_SAMPLED_BIT),
          static_cast<VkImageUsageFlags>(
              VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)}) {
      if ((capabilities.supportedUsageFlags & optional) != 0) {
        usage |= optional;
      }
    }
    if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0) {
      say("the surface's images cannot be sampled, and this backend has no "
          "other way to be handed one");
      return false;
    }

    // Opaque where it is offered, which is what a game window is. Where it
    // is not, the compositor decides, and what it decides is better than a
    // swapchain that was refused.
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & composite) == 0) {
      if ((capabilities.supportedCompositeAlpha &
           VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
        composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
      } else if ((capabilities.supportedCompositeAlpha &
                  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
        composite = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
      } else {
        composite = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
      }
    }

    VkSurfaceTransformFlagBitsKHR transform = capabilities.currentTransform;
    if ((capabilities.supportedTransforms & transform) == 0) {
      transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }

    // And the way images reach the screen, which is the last thing this was
    // asserting rather than asking. FIFO is the one the specification says
    // every implementation has, which is not the same as every surface
    // offering it -- and a surface refuses the whole description over it
    // just as it does over any other bit.
    std::uint32_t modes = 0;
    fApi.fGetSurfacePresentModes(fPhysical, fSurface, &modes, nullptr);
    std::vector<VkPresentModeKHR> present(modes);
    if (modes > 0) {
      fApi.fGetSurfacePresentModes(fPhysical, fSurface, &modes, present.data());
    }
    const auto offers = [&present](VkPresentModeKHR mode) {
      return std::find(present.begin(), present.end(), mode) != present.end();
    };
    // What the client asked for, in the order that gives it.
    //
    // Waiting for the display is FIFO, and mailbox is the same wait without
    // the queue -- both hold the frame rate to the screen's. Not waiting is
    // immediate, which is what a frame limiter that is switched off means:
    // the client draws as fast as it can and says so. This was FIFO whatever
    // the setting said, which is a client showing sixty frames a second with
    // its limiter off and no way to tell why.
    std::vector<VkPresentModeKHR> wanted;
    if (fWaitForDisplay) {
      wanted = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                VK_PRESENT_MODE_IMMEDIATE_KHR};
    } else {
      // Mailbox first, and that is a measurement rather than a preference:
      // on a phone running phosh with Turnip the three arrangements came out
      // at 11.0, 11.9 and 11.2 milliseconds a frame, with mailbox lowest and
      // its submit a third cheaper. It also shows whole frames, where
      // immediate can tear -- so where they are this close, the one that
      // does not tear wins.
      wanted = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
                VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_FIFO_KHR};
    }
    if (const char *said = std::getenv("OSU_VULKAN_PRESENT")) {
      const std::string_view named(said);
      const auto asked =
          named == "immediate"  ? VK_PRESENT_MODE_IMMEDIATE_KHR
          : named == "mailbox"  ? VK_PRESENT_MODE_MAILBOX_KHR
          : named == "relaxed"  ? VK_PRESENT_MODE_FIFO_RELAXED_KHR
                                : VK_PRESENT_MODE_FIFO_KHR;
      wanted.insert(wanted.begin(), asked);
    }
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    bool settled = false;
    for (const VkPresentModeKHR candidate : wanted) {
      if (offers(candidate)) {
        mode = candidate;
        settled = true;
        break;
      }
    }
    if (!settled) {
      if (present.empty()) {
        say("the surface offers no way of presenting an image");
        return false;
      }
      mode = present.front();
    }

    VkSwapchainCreateInfoKHR create{};
    create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create.surface = fSurface;
    create.minImageCount = images;
    create.imageFormat = chosen.format;
    create.imageColorSpace = chosen.colorSpace;
    create.imageExtent = extent;
    create.imageArrayLayers = 1;
    // Drawn into by Skia, and copied out of by nothing: what a colour
    // attachment is for. Transfer destination as well, because clearing a
    // whole image is a transfer on some drivers.
    create.imageUsage = usage;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = transform;
    create.compositeAlpha = composite;
    create.presentMode = mode;
    create.clipped = VK_TRUE;
    const VkResult made =
        fApi.fCreateSwapchain(fDevice, &create, nullptr, &fSwapchain);
    if (made != VK_SUCCESS) {
      // Everything the driver was asked for and everything it said it could
      // do, because "no swapchain" on its own is a message that cannot be
      // acted on.
      say(std::format(
          "no swapchain for this surface: VkResult {}. asked for {}x{} "
          "format {} colour space {} images {} usage {:#x} composite {:#x} "
          "transform {:#x} present mode {}; the surface offers extent {}x{} "
          "(min {}x{}, max {}x{}) images {}..{} usage {:#x} composite {:#x} "
          "transforms {:#x} and {} present mode(s)",
          static_cast<int>(made), extent.width, extent.height,
          static_cast<int>(chosen.format), static_cast<int>(chosen.colorSpace),
          images, static_cast<unsigned>(usage),
          static_cast<unsigned>(composite), static_cast<unsigned>(transform),
          static_cast<int>(mode),
          capabilities.currentExtent.width, capabilities.currentExtent.height,
          capabilities.minImageExtent.width,
          capabilities.minImageExtent.height,
          capabilities.maxImageExtent.width,
          capabilities.maxImageExtent.height, capabilities.minImageCount,
          capabilities.maxImageCount,
          static_cast<unsigned>(capabilities.supportedUsageFlags),
          static_cast<unsigned>(capabilities.supportedCompositeAlpha),
          static_cast<unsigned>(capabilities.supportedTransforms),
          present.size()));
      return false;
    }

    std::uint32_t count = 0;
    fApi.fGetSwapchainImages(fDevice, fSwapchain, &count, nullptr);
    fImages.resize(count);
    fApi.fGetSwapchainImages(fDevice, fSwapchain, &count, fImages.data());

    // One surface per image, made once: wrapping is cheap but not free, and
    // the images do not change until the swapchain does.
    const skia::graphite::VulkanTextureInfo info(
        VK_SAMPLE_COUNT_1_BIT, skia::graphite::Mipmapped::kNo, /*flags=*/0,
        chosen.format, VK_IMAGE_TILING_OPTIMAL, create.imageUsage,
        VK_SHARING_MODE_EXCLUSIVE, VK_IMAGE_ASPECT_COLOR_BIT,
        skia::graphite::VulkanYcbcrConversionInfo{});

    fSurfaces.clear();
    fSurfaces.reserve(fImages.size());
    for (VkImage image : fImages) {
      const skia::graphite::BackendTexture texture =
          skia::graphite::backendTextures::MakeVulkan(
              skia::SkISize::Make(static_cast<int>(extent.width),
                                  static_cast<int>(extent.height)),
              info, VK_IMAGE_LAYOUT_UNDEFINED, fQueueFamily, image,
              skia::graphite::VulkanAlloc{});
      // No colour type given: it is read out of the image's own format,
      // which is the one the surface said it wanted.
      skia::Sp<skia::SkSurface> surface = skia::graphite::WrapBackendTexture(
          fRecorder.get(), texture, /*colorSpace=*/nullptr, /*props=*/nullptr);
      if (!surface) {
        say("a swapchain image could not be wrapped");
        return false;
      }
      fSurfaces.push_back(std::move(surface));
    }

    // A semaphore pair per image in flight, made once with the swapchain.
    for (Slot &slot : fSlots) {
      if (slot.fAcquired != VK_NULL_HANDLE) {
        fApi.fDestroySemaphore(fDevice, slot.fAcquired, nullptr);
      }
      if (slot.fRendered != VK_NULL_HANDLE) {
        fApi.fDestroySemaphore(fDevice, slot.fRendered, nullptr);
      }
    }
    fSlots.assign(fImages.size(), Slot{});
    // A new swapchain is new images: nothing in them is this client's, and
    // the first frame into each has to be a whole one.
    fShownAt.assign(fImages.size(), 0);
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (Slot &slot : fSlots) {
      if (fApi.fCreateSemaphore(fDevice, &semaphore, nullptr,
                                &slot.fAcquired) != VK_SUCCESS ||
          fApi.fCreateSemaphore(fDevice, &semaphore, nullptr,
                                &slot.fRendered) != VK_SUCCESS) {
        say("no semaphores");
        return false;
      }
    }

    fWidth = static_cast<int>(extent.width);
    fHeight = static_cast<int>(extent.height);
    fSwapchainStale = false;
    std::println(std::cerr,
                 "[vulkan] swapchain {}x{}, {} images, format {}, present "
                 "mode {} (asked for {}x{})",
                 fWidth, fHeight, fImages.size(),
                 static_cast<int>(chosen.format), static_cast<int>(mode),
                 width, height);
    return true;
  }

  ::GLFWwindow *fWindow = nullptr;
  Api fApi;
  VkInstance fInstance = VK_NULL_HANDLE;
  VkSurfaceKHR fSurface = VK_NULL_HANDLE;
  VkPhysicalDevice fPhysical = VK_NULL_HANDLE;
  VkDevice fDevice = VK_NULL_HANDLE;
  VkQueue fQueue = VK_NULL_HANDLE;
  std::uint32_t fQueueFamily = 0;
  std::string fDeviceName;
  std::vector<const char *> fInstanceExtensions;
  std::vector<const char *> fDeviceExtensions;
  skia::graphite::VulkanExtensions fExtensions;

  skia::Sp<Images> fImageProvider;
  std::unique_ptr<skia::graphite::Context> fContext;
  std::unique_ptr<skia::graphite::Recorder> fRecorder;

  VkSwapchainKHR fSwapchain = VK_NULL_HANDLE;
  std::vector<VkImage> fImages;
  std::vector<skia::Sp<skia::SkSurface>> fSurfaces;
  std::vector<Slot> fSlots;
  std::uint32_t fImage = 0;
  int fSlot = -1;
  int fWidth = 0;
  int fHeight = 0;
  int fFailedWidth = 0;
  int fFailedHeight = 0;
  bool fSwapchainStale = false;
  bool fWaitForDisplay = true;
  const bool fTiming = std::getenv("OSU_VULKAN_TIMING") != nullptr;
  std::int64_t fAcquireUs = 0;
  std::int64_t fSnapUs = 0;
  std::int64_t fSubmitUs = 0;
  std::int64_t fPresentUs = 0;
  std::uint64_t fFrames = 0;
  std::vector<std::uint64_t> fShownAt;
  std::int64_t fWaitUs = 0;
  std::int64_t fDrawUs = 0;
  std::chrono::steady_clock::time_point fDrawFrom{};
  int fTimed = 0;
};

} // namespace platform::vulkan
