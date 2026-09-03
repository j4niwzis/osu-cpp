package android.content;

import android.net.Uri;

public abstract class ContentResolver {
    public abstract void takePersistableUriPermission(Uri uri, int flags);
}
