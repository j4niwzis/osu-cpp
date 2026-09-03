package io.github.j4niwzis.osu_cpp;

import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;

public final class OsuNativeActivity extends NativeActivity {
    // NativeActivity opens the library itself, with dlopen, to find
    // ANativeActivity_onCreate. A library opened that way is not one of this
    // class loader's, and the runtime looking for the implementation of the
    // native method below does not search it -- "No implementation found",
    // with the symbol exported all along. This is what registers it.
    static {
        System.loadLibrary("osu_client");
    }

    private static final int OPEN_BEATMAP = 0x4f53;
    private static final int CREATE_VIDEO = 0x4f54;

    private native void nativeDocumentSelected(int requestCode, String uri);

    public void openBeatmapPicker() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                intent.setType("application/octet-stream");
                intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                    "application/x-osu-beatmap-archive",
                    "application/zip",
                    "application/octet-stream"
                });
                startActivityForResult(intent, OPEN_BEATMAP);
            }
        });
    }

    public void createVideoFile(final String suggestedName) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("video/mp4");
                intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
                intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                startActivityForResult(intent, CREATE_VIDEO);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode,
                                    Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != OPEN_BEATMAP && requestCode != CREATE_VIDEO) {
            return;
        }
        Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (uri != null) {
            try {
                getContentResolver().takePersistableUriPermission(
                    uri, data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
            } catch (SecurityException ignored) {
                // Some providers grant access only for the lifetime of this activity.
            }
        }
        nativeDocumentSelected(requestCode, uri == null ? null : uri.toString());
    }
}
