// classes.dex for the activity this APK carries.
//
// What this replaces is javac and d8: a JDK and twenty megabytes of somebody
// else's compiler, for three classes and a hundred and twenty instructions.
// The Java beside it (android/java/.../OsuNativeActivity.java) stays as the
// statement of what these classes do -- it is what a reader should read --
// and this is that statement in the form Android loads.
//
// The registers are assigned by hand. For methods this size that is clearer
// than anything that would assign them, and it is checked: every instruction
// form here refuses a register it cannot encode.

import std;
import dex.file;

namespace {

const std::string kActivity = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity;";
const std::string kPicker = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity$1;";
const std::string kCreator = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity$2;";
const std::string kNativeActivity = "Landroid/app/NativeActivity;";
const std::string kIntent = "Landroid/content/Intent;";
const std::string kUri = "Landroid/net/Uri;";
const std::string kResolver = "Landroid/content/ContentResolver;";
const std::string kObject = "Ljava/lang/Object;";
const std::string kRunnable = "Ljava/lang/Runnable;";
const std::string kString = "Ljava/lang/String;";
const std::string kStrings = "[Ljava/lang/String;";
const std::string kSecurity = "Ljava/lang/SecurityException;";
const std::string kSystem = "Ljava/lang/System;";
const std::string kInt = "I";
const std::string kVoid = "V";

// What the two requests are called when they come back.
constexpr int kOpenBeatmap = 0x4F53;
constexpr int kCreateVideo = 0x4F54;

// android.content.Intent's flags, which are API and not implementation.
constexpr int kGrantRead = 0x00000001;
constexpr int kGrantWrite = 0x00000002;
constexpr int kGrantPersistable = 0x00000040;
constexpr int kResultOk = -1;

constexpr std::uint32_t kPublic = 0x1;
constexpr std::uint32_t kPrivate = 0x2;
constexpr std::uint32_t kProtected = 0x4;
constexpr std::uint32_t kFinal = 0x10;
constexpr std::uint32_t kSynthetic = 0x1000;
constexpr std::uint32_t kNative = 0x100;
constexpr std::uint32_t kStatic = 0x8;
constexpr std::uint32_t kConstructor = 0x10000;

// The three classes, described twice -- once to collect what they name and
// once to emit them -- which is why this is a function.
[[nodiscard]] std::vector<dex::Class> build(dex::Pool &pool) {
  std::vector<dex::Class> classes;

  // -- the Runnable that opens a beatmap -----------------------------------
  dex::Class picker(pool, kPicker, kObject, {kRunnable}, 0);
  picker.field("this$0", kActivity, kFinal | kSynthetic);

  auto code = std::make_shared<dex::Code>(pool, 2, 2, 1);
  code->writeField(1, 0, {kPicker, "this$0", kActivity});
  code->callDirect({0}, {kObject, "<init>", kVoid, {}});
  code->returnVoid();
  picker.method("<init>", kVoid, {kActivity}, kConstructor, code, true);

  code = std::make_shared<dex::Code>(pool, 6, 1, 3);
  {
    constexpr std::uint8_t self = 5;
    code->newInstance(0, kIntent);
    code->constantString(1, "android.intent.action.OPEN_DOCUMENT");
    code->callDirect({0, 1}, {kIntent, "<init>", kVoid, {kString}});
    code->constantString(1, "android.intent.category.OPENABLE");
    code->callVirtual({0, 1}, {kIntent, "addCategory", kIntent, {kString}});
    code->constant16(1, kGrantRead | kGrantPersistable);
    code->callVirtual({0, 1}, {kIntent, "addFlags", kIntent, {kInt}});
    code->constantString(1, "application/octet-stream");
    code->callVirtual({0, 1}, {kIntent, "setType", kIntent, {kString}});
    code->constant4(2, 3);
    code->newArray(4, 2, kStrings);
    int index = 0;
    for (const char *mime : {"application/x-osu-beatmap-archive",
                             "application/zip", "application/octet-stream"}) {
      code->constant4(2, index++);
      code->constantString(3, mime);
      code->putObject(3, 4, 2);
    }
    code->constantString(1, "android.intent.extra.MIME_TYPES");
    code->callVirtual({0, 1, 4},
                      {kIntent, "putExtra", kIntent, {kString, kStrings}});
    code->readField(1, self, {kPicker, "this$0", kActivity});
    code->constant16(2, kOpenBeatmap);
    code->callVirtual(
        {1, 0, 2},
        {kActivity, "startActivityForResult", kVoid, {kIntent, kInt}});
    code->returnVoid();
  }
  picker.method("run", kVoid, {}, kPublic, code, false);
  classes.push_back(std::move(picker));

  // -- the Runnable that creates a video file -------------------------------
  dex::Class creator(pool, kCreator, kObject, {kRunnable}, 0);
  creator.field("this$0", kActivity, kFinal | kSynthetic);
  creator.field("val$suggestedName", kString, kFinal | kSynthetic);

  code = std::make_shared<dex::Code>(pool, 3, 3, 1);
  code->writeField(1, 0, {kCreator, "this$0", kActivity});
  code->writeField(2, 0, {kCreator, "val$suggestedName", kString});
  code->callDirect({0}, {kObject, "<init>", kVoid, {}});
  code->returnVoid();
  creator.method("<init>", kVoid, {kActivity, kString}, kConstructor, code,
                 true);

  code = std::make_shared<dex::Code>(pool, 4, 1, 3);
  {
    constexpr std::uint8_t self = 3;
    code->newInstance(0, kIntent);
    code->constantString(1, "android.intent.action.CREATE_DOCUMENT");
    code->callDirect({0, 1}, {kIntent, "<init>", kVoid, {kString}});
    code->constantString(1, "android.intent.category.OPENABLE");
    code->callVirtual({0, 1}, {kIntent, "addCategory", kIntent, {kString}});
    code->constantString(1, "video/mp4");
    code->callVirtual({0, 1}, {kIntent, "setType", kIntent, {kString}});
    code->constantString(1, "android.intent.extra.TITLE");
    code->readField(2, self, {kCreator, "val$suggestedName", kString});
    code->callVirtual({0, 1, 2},
                      {kIntent, "putExtra", kIntent, {kString, kString}});
    code->constant16(1, kGrantWrite | kGrantPersistable);
    code->callVirtual({0, 1}, {kIntent, "addFlags", kIntent, {kInt}});
    code->readField(1, self, {kCreator, "this$0", kActivity});
    code->constant16(2, kCreateVideo);
    code->callVirtual(
        {1, 0, 2},
        {kActivity, "startActivityForResult", kVoid, {kIntent, kInt}});
    code->returnVoid();
  }
  creator.method("run", kVoid, {}, kPublic, code, false);
  classes.push_back(std::move(creator));

  // -- the activity ---------------------------------------------------------
  dex::Class activity(pool, kActivity, kNativeActivity, {}, kPublic | kFinal);

  // The library, loaded by name before anything in this class runs.
  //
  // NativeActivity opens it itself, with dlopen, to find
  // ANativeActivity_onCreate -- and a library opened that way is not one of
  // the class loader's, so the runtime looking for the implementation of a
  // native method declared here does not search it. It says so exactly:
  //
  //   No implementation found for void ...nativeDocumentSelected(int, String)
  //   (tried Java_..._nativeDocumentSelected and ...__ILjava_lang_String_2)
  //
  // -- with the symbol present in the library all along. System.loadLibrary
  // is what registers it against this class loader, and it is the same
  // library either way: the second open of the same soname returns the first.
  code = std::make_shared<dex::Code>(pool, 1, 0, 1);
  code->constantString(0, "osu_client");
  code->callStatic({0}, {kSystem, "loadLibrary", kVoid, {kString}});
  code->returnVoid();
  activity.method("<clinit>", kVoid, {}, kStatic | kConstructor, code, true);

  code = std::make_shared<dex::Code>(pool, 1, 1, 1);
  code->callDirect({0}, {kNativeActivity, "<init>", kVoid, {}});
  code->returnVoid();
  activity.method("<init>", kVoid, {}, kPublic | kConstructor, code, true);
  activity.method("nativeDocumentSelected", kVoid, {kInt, kString},
                  kPrivate | kNative, nullptr, true);

  code = std::make_shared<dex::Code>(pool, 2, 1, 2);
  code->newInstance(0, kPicker);
  code->callDirect({0, 1}, {kPicker, "<init>", kVoid, {kActivity}});
  code->callVirtual({1, 0}, {kActivity, "runOnUiThread", kVoid, {kRunnable}});
  code->returnVoid();
  activity.method("openBeatmapPicker", kVoid, {}, kPublic, code, false);

  code = std::make_shared<dex::Code>(pool, 3, 2, 3);
  code->newInstance(0, kCreator);
  code->callDirect({0, 1, 2}, {kCreator, "<init>", kVoid, {kActivity, kString}});
  code->callVirtual({1, 0}, {kActivity, "runOnUiThread", kVoid, {kRunnable}});
  code->returnVoid();
  activity.method("createVideoFile", kVoid, {kString}, kPublic, code, false);

  // onActivityResult: v4 this, v5 request, v6 result, v7 data; v0 to v2
  // spare, and v1 the address being worked out.
  code = std::make_shared<dex::Code>(pool, 8, 4, 4);
  code->callSuper({4, 5, 6, 7},
                  {kNativeActivity, "onActivityResult", kVoid,
                   {kInt, kInt, kIntent}});
  code->constant16(0, kOpenBeatmap);
  code->jumpIfEqual(5, 0, "ours");
  code->constant16(0, kCreateVideo);
  code->jumpIfEqual(5, 0, "ours");
  code->returnVoid();

  code->label("ours");
  code->constant4(1, 0); // no address yet
  code->constant4(0, kResultOk);
  code->jumpIfDifferent(6, 0, "decided");
  code->jumpIfZero(7, "decided");
  code->callVirtual({7}, {kIntent, "getData", kUri, {}});
  code->moveResultObject(1);

  code->label("decided");
  code->jumpIfZero(1, "report");
  code->label("try");
  code->callVirtual({4}, {kActivity, "getContentResolver", kResolver, {}});
  code->moveResultObject(0);
  code->callVirtual({7}, {kIntent, "getFlags", kInt, {}});
  code->moveResult(2);
  code->andConstant(2, 2, kGrantRead | kGrantWrite);
  code->callVirtual(
      {0, 1, 2},
      {kResolver, "takePersistableUriPermission", kVoid, {kUri, kInt}});
  code->label("done");
  code->jump("report");

  // Some providers grant access only for the lifetime of this activity, and
  // say so by throwing. There is nothing to do about it.
  code->label("caught");
  code->moveException(0);

  code->label("report");
  code->jumpIfNotZero(1, "have");
  code->constant4(2, 0);
  code->jump("tell");
  code->label("have");
  code->callVirtual({1}, {kUri, "toString", kString, {}});
  code->moveResultObject(2);
  code->label("tell");
  code->callDirect({4, 5, 2},
                   {kActivity, "nativeDocumentSelected", kVoid,
                    {kInt, kString}});
  code->returnVoid();
  code->guard("try", "done", "caught", kSecurity);
  activity.method("onActivityResult", kVoid, {kInt, kInt, kIntent}, kProtected,
                  code, false);
  classes.push_back(std::move(activity));
  return classes;
}

} // namespace

int main(int count, char **arguments) {
  std::filesystem::path output;
  for (int at = 1; at + 1 < count; ++at) {
    if (std::string_view(arguments[at]) == "--out") {
      output = arguments[at + 1];
    }
  }
  if (output.empty()) {
    std::println(std::cerr, "usage: {} --out <classes.dex>", arguments[0]);
    return 2;
  }

  dex::Pool pool;
  (void)build(pool); // what it names
  pool.freeze();
  auto classes = build(pool); // and what it is
  const auto blob = dex::write(pool, classes);

  std::error_code failed;
  std::filesystem::create_directories(output.parent_path(), failed);
  std::ofstream file(output, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(blob.data()),
             static_cast<std::streamsize>(blob.size()));
  if (!file) {
    std::println(std::cerr, "{}: cannot be written", output.string());
    return 1;
  }
  std::println("{}: {} bytes", output.string(), blob.size());
  return 0;
}
