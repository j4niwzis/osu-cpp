package android.app;

import android.content.ContentResolver;
import android.content.Intent;

public class Activity {
    public static final int RESULT_OK = -1;
    public void runOnUiThread(Runnable action) {}
    public void startActivityForResult(Intent intent, int requestCode) {}
    public ContentResolver getContentResolver() { return null; }
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {}
}
