#!/usr/bin/env python3
"""classes.dex for the activity this APK carries.

What this replaces is javac and d8: a JDK and twenty megabytes of somebody
else's compiler, for three classes and a hundred and twenty instructions. The
Java beside it (android/java/.../OsuNativeActivity.java) stays as the
statement of what these classes do -- it is what a reader should read -- and
this is that statement in the form Android loads.

The registers are assigned by hand. For methods this size that is clearer
than anything that would assign them, and it is checked: every instruction
form here refuses a register it cannot encode.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dexwrite import Class, Code, Dex, write

ACTIVITY = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity;"
PICKER = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity$1;"
CREATOR = "Lio/github/j4niwzis/osu_cpp/OsuNativeActivity$2;"
NATIVE_ACTIVITY = "Landroid/app/NativeActivity;"
INTENT = "Landroid/content/Intent;"
URI = "Landroid/net/Uri;"
RESOLVER = "Landroid/content/ContentResolver;"
OBJECT = "Ljava/lang/Object;"
RUNNABLE = "Ljava/lang/Runnable;"
STRING = "Ljava/lang/String;"
STRINGS = "[Ljava/lang/String;"
SECURITY = "Ljava/lang/SecurityException;"

# What the two requests are called when they come back.
OPEN_BEATMAP = 0x4F53
CREATE_VIDEO = 0x4F54

# android.content.Intent's flags, which are API and not implementation.
FLAG_GRANT_READ = 0x00000001
FLAG_GRANT_WRITE = 0x00000002
FLAG_GRANT_PERSISTABLE = 0x00000040
RESULT_OK = -1

PUBLIC = 0x1
PRIVATE = 0x2
PROTECTED = 0x4
STATIC = 0x8
FINAL = 0x10
NATIVE = 0x100
CONSTRUCTOR = 0x10000


def build(dex):
    """The three classes, described twice -- once to collect what they name
    and once to emit them -- which is why this is a function."""
    classes = []

    # -- the Runnable that opens a beatmap ---------------------------------
    picker = Class(dex, PICKER, OBJECT, [RUNNABLE], 0x0)
    picker.field("this$0", ACTIVITY, FINAL | 0x1000)  # synthetic

    code = Code(dex, registers=2, ins=2, outs=1)
    code.iput_object(1, 0, (PICKER, "this$0", ACTIVITY))
    code.invoke_direct([0], (OBJECT, "<init>", "V", []))
    code.return_void()
    picker.method("<init>", "V", [ACTIVITY], CONSTRUCTOR, code, direct=True)

    code = Code(dex, registers=6, ins=1, outs=3)
    this = 5
    code.new_instance(0, INTENT)
    code.const_string(1, "android.intent.action.OPEN_DOCUMENT")
    code.invoke_direct([0, 1], (INTENT, "<init>", "V", [STRING]))
    code.const_string(1, "android.intent.category.OPENABLE")
    code.invoke_virtual([0, 1], (INTENT, "addCategory", INTENT, [STRING]))
    code.const16(1, FLAG_GRANT_READ | FLAG_GRANT_PERSISTABLE)
    code.invoke_virtual([0, 1], (INTENT, "addFlags", INTENT, ["I"]))
    code.const_string(1, "application/octet-stream")
    code.invoke_virtual([0, 1], (INTENT, "setType", INTENT, [STRING]))
    code.const4(2, 3)
    code.new_array(4, 2, STRINGS)
    for index, mime in enumerate(("application/x-osu-beatmap-archive",
                                  "application/zip",
                                  "application/octet-stream")):
        code.const4(2, index)
        code.const_string(3, mime)
        code.aput_object(3, 4, 2)
    code.const_string(1, "android.intent.extra.MIME_TYPES")
    code.invoke_virtual([0, 1, 4], (INTENT, "putExtra", INTENT, [STRING, STRINGS]))
    code.iget_object(1, this, (PICKER, "this$0", ACTIVITY))
    code.const16(2, OPEN_BEATMAP)
    code.invoke_virtual([1, 0, 2],
                        (ACTIVITY, "startActivityForResult", "V", [INTENT, "I"]))
    code.return_void()
    picker.method("run", "V", [], PUBLIC, code)
    classes.append(picker)

    # -- the Runnable that creates a video file ----------------------------
    creator = Class(dex, CREATOR, OBJECT, [RUNNABLE], 0x0)
    creator.field("this$0", ACTIVITY, FINAL | 0x1000)
    creator.field("val$suggestedName", STRING, FINAL | 0x1000)

    code = Code(dex, registers=3, ins=3, outs=1)
    code.iput_object(1, 0, (CREATOR, "this$0", ACTIVITY))
    code.iput_object(2, 0, (CREATOR, "val$suggestedName", STRING))
    code.invoke_direct([0], (OBJECT, "<init>", "V", []))
    code.return_void()
    creator.method("<init>", "V", [ACTIVITY, STRING], CONSTRUCTOR, code, direct=True)

    code = Code(dex, registers=4, ins=1, outs=3)
    this = 3
    code.new_instance(0, INTENT)
    code.const_string(1, "android.intent.action.CREATE_DOCUMENT")
    code.invoke_direct([0, 1], (INTENT, "<init>", "V", [STRING]))
    code.const_string(1, "android.intent.category.OPENABLE")
    code.invoke_virtual([0, 1], (INTENT, "addCategory", INTENT, [STRING]))
    code.const_string(1, "video/mp4")
    code.invoke_virtual([0, 1], (INTENT, "setType", INTENT, [STRING]))
    code.const_string(1, "android.intent.extra.TITLE")
    code.iget_object(2, this, (CREATOR, "val$suggestedName", STRING))
    code.invoke_virtual([0, 1, 2], (INTENT, "putExtra", INTENT, [STRING, STRING]))
    code.const16(1, FLAG_GRANT_WRITE | FLAG_GRANT_PERSISTABLE)
    code.invoke_virtual([0, 1], (INTENT, "addFlags", INTENT, ["I"]))
    code.iget_object(1, this, (CREATOR, "this$0", ACTIVITY))
    code.const16(2, CREATE_VIDEO)
    code.invoke_virtual([1, 0, 2],
                        (ACTIVITY, "startActivityForResult", "V", [INTENT, "I"]))
    code.return_void()
    creator.method("run", "V", [], PUBLIC, code)
    classes.append(creator)

    # -- the activity ------------------------------------------------------
    activity = Class(dex, ACTIVITY, NATIVE_ACTIVITY, [], PUBLIC | FINAL)

    code = Code(dex, registers=1, ins=1, outs=1)
    code.invoke_direct([0], (NATIVE_ACTIVITY, "<init>", "V", []))
    code.return_void()
    activity.method("<init>", "V", [], PUBLIC | CONSTRUCTOR, code, direct=True)
    activity.method("nativeDocumentSelected", "V", ["I", STRING],
                    PRIVATE | NATIVE, None, direct=True)

    code = Code(dex, registers=2, ins=1, outs=2)
    code.new_instance(0, PICKER)
    code.invoke_direct([0, 1], (PICKER, "<init>", "V", [ACTIVITY]))
    code.invoke_virtual([1, 0], (ACTIVITY, "runOnUiThread", "V", [RUNNABLE]))
    code.return_void()
    activity.method("openBeatmapPicker", "V", [], PUBLIC, code)

    code = Code(dex, registers=3, ins=2, outs=3)
    code.new_instance(0, CREATOR)
    code.invoke_direct([0, 1, 2], (CREATOR, "<init>", "V", [ACTIVITY, STRING]))
    code.invoke_virtual([1, 0], (ACTIVITY, "runOnUiThread", "V", [RUNNABLE]))
    code.return_void()
    activity.method("createVideoFile", "V", [STRING], PUBLIC, code)

    # onActivityResult: v4 this, v5 request, v6 result, v7 data; v0..v2 spare
    # and v1 the uri that is being worked out.
    code = Code(dex, registers=8, ins=4, outs=4)
    code.invoke_super([4, 5, 6, 7],
                      (NATIVE_ACTIVITY, "onActivityResult", "V",
                       ["I", "I", INTENT]))
    code.const16(0, OPEN_BEATMAP)
    code.if_eq(5, 0, "ours")
    code.const16(0, CREATE_VIDEO)
    code.if_eq(5, 0, "ours")
    code.return_void()

    code.label("ours")
    code.const4(1, 0)                       # uri = null
    code.const4(0, RESULT_OK)
    code.if_ne(6, 0, "decided")
    code.if_eqz(7, "decided")
    code.invoke_virtual([7], (INTENT, "getData", URI, []))
    code.move_result_object(1)

    code.label("decided")
    code.if_eqz(1, "report")
    code.label("try")
    code.invoke_virtual([4], (ACTIVITY, "getContentResolver", RESOLVER, []))
    code.move_result_object(0)
    code.invoke_virtual([7], (INTENT, "getFlags", "I", []))
    code.move_result(2)
    code.and_int_lit8(2, 2, FLAG_GRANT_READ | FLAG_GRANT_WRITE)
    code.invoke_virtual([0, 1, 2],
                        (RESOLVER, "takePersistableUriPermission", "V",
                         [URI, "I"]))
    code.label("done")
    code.goto("report")

    # Some providers grant access only for the lifetime of this activity,
    # and say so by throwing. There is nothing to do about it.
    code.label("caught")
    code.move_exception(0)

    code.label("report")
    code.if_nez(1, "have")
    code.const4(2, 0)
    code.goto("tell")
    code.label("have")
    code.invoke_virtual([1], (URI, "toString", STRING, []))
    code.move_result_object(2)
    code.label("tell")
    code.invoke_direct([4, 5, 2],
                       (ACTIVITY, "nativeDocumentSelected", "V", ["I", STRING]))
    code.return_void()
    code.try_catch("try", "done", "caught", SECURITY)
    activity.method("onActivityResult", "V", ["I", "I", INTENT], PROTECTED, code)
    classes.append(activity)
    return classes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    dex = Dex()
    build(dex)          # what it names
    dex.freeze()
    classes = build(dex)  # and what it is
    blob = write(dex, classes)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as out:
        out.write(blob)
    print("%s: %d bytes" % (args.out, len(blob)))


if __name__ == "__main__":
    main()
