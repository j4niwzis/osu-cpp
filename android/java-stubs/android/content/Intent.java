package android.content;

import android.net.Uri;

public class Intent {
    public static final String ACTION_OPEN_DOCUMENT =
        "android.intent.action.OPEN_DOCUMENT";
    public static final String ACTION_CREATE_DOCUMENT =
        "android.intent.action.CREATE_DOCUMENT";
    public static final String CATEGORY_OPENABLE =
        "android.intent.category.OPENABLE";
    public static final String EXTRA_MIME_TYPES =
        "android.intent.extra.MIME_TYPES";
    public static final String EXTRA_TITLE = "android.intent.extra.TITLE";
    public static final int FLAG_GRANT_READ_URI_PERMISSION = 1;
    public static final int FLAG_GRANT_WRITE_URI_PERMISSION = 2;
    public static final int FLAG_GRANT_PERSISTABLE_URI_PERMISSION = 64;

    public Intent(String action) {}
    public Intent addCategory(String category) { return this; }
    public Intent addFlags(int flags) { return this; }
    public Intent setType(String type) { return this; }
    public Intent putExtra(String name, String value) { return this; }
    public Intent putExtra(String name, String[] value) { return this; }
    public Uri getData() { return null; }
    public int getFlags() { return 0; }
}
