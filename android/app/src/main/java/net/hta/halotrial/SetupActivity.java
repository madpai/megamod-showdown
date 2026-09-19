package net.hta.halotrial;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * First-run helper: the user picks their own Trial map via the system document
 * picker, we copy it into app-private storage, then NativeActivity loads it
 * with no extra permissions. Android 11+ hides Android/data/ from file
 * managers and All-files access is a special setting, so a picker is the
 * reliable remote-test path.
 */
public class SetupActivity extends Activity {
    private static final String TAG = "halo-trial-android";
    private static final int REQ_PICK_MAP = 1;
    private static final int REQ_PICK_BITM = 2;
    private static final int REQ_PICK_SND = 3;

    private TextView status;
    private Button play;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF11151C);
        int pad = dp(20);
        root.setPadding(pad, pad, pad, pad);
        root.setGravity(Gravity.CENTER_HORIZONTAL);

        Button pick = btn("Pick bloodgulch.map", 0xFF2B6CF6);
        pick.setOnClickListener(v -> pickFile(REQ_PICK_MAP));
        Button pickBm = btn("Pick bitmaps.map (textures)", 0xFF2B6CF6);
        pickBm.setOnClickListener(v -> pickFile(REQ_PICK_BITM));
        Button pickSnd = btn("Pick sounds.map (audio)", 0xFF2B6CF6);
        pickSnd.setOnClickListener(v -> pickFile(REQ_PICK_SND));

        play = btn("Play", 0xFF2A7A3A);
        play.setOnClickListener(v -> launchGame());

        status = tv("", 14, 0xFF8FB6FF, false);

        root.addView(tv("Halo Trial PoC", 22, 0xFFE6E9EF, true));
        root.addView(space(12));
        root.addView(tv(
                "This app does not bundle any Halo files.\n"
                        + "Pick bloodgulch.map, then bitmaps.map (79 MB) and "
                        + "sounds.map (76 MB), from the same Trial maps folder. "
                        + "Without bitmaps.map the world stays untextured; "
                        + "without sounds.map it stays silent.",
                15, 0xFF94A0B4, false));
        root.addView(space(24));
        root.addView(pick);
        root.addView(space(10));
        root.addView(pickBm);
        root.addView(space(10));
        root.addView(pickSnd);
        root.addView(space(10));
        root.addView(play);
        root.addView(space(16));
        root.addView(status);
        setContentView(root);

        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        File map = existingMap();
        File bitm = existingBitmaps();
        File snd = existingSounds();
        if (map != null) {
            play.setEnabled(true);
            play.setAlpha(1f);
            String msg = "Map ready: " + map.getName() + " ("
                    + (map.length() / 1024 / 1024) + " MB).";
            msg += bitm != null
                    ? " Textures: bitmaps.map (" + (bitm.length() / 1024 / 1024) + " MB)."
                    : " No bitmaps.map — untextured.";
            msg += snd != null
                    ? " Audio: sounds.map (" + (snd.length() / 1024 / 1024) + " MB)."
                    : " No sounds.map — silent.";
            if (bitm != null && snd != null) msg += " Tap Play.";
            status.setText(msg);
        } else {
            play.setEnabled(false);
            play.setAlpha(0.4f);
            status.setText("No map in app storage yet.");
        }
    }

    private File destDir() {
        File ext = getExternalFilesDir(null);
        return ext != null ? ext : getFilesDir();
    }

    private File existingMap() {
        File dir = destDir();
        File named = new File(dir, "bloodgulch.map");
        if (named.isFile() && named.length() > 0) return named;
        File[] files = dir.listFiles();
        if (files == null) return null;
        for (File f : files) {
            String n = f.getName();
            if (n.length() > 4
                    && n.substring(n.length() - 4).equalsIgnoreCase(".map")
                    && f.isFile() && f.length() > 0
                    && !n.equalsIgnoreCase("bitmaps.map")
                    && !n.equalsIgnoreCase("sounds.map")
                    && !n.equalsIgnoreCase("ui.map")) {
                return f;
            }
        }
        return null;
    }

    private File existingBitmaps() {
        File f = new File(destDir(), "bitmaps.map");
        return (f.isFile() && f.length() > 0) ? f : null;
    }

    private File existingSounds() {
        File f = new File(destDir(), "sounds.map");
        return (f.isFile() && f.length() > 0) ? f : null;
    }

    private void pickFile(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, req);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK_MAP && requestCode != REQ_PICK_BITM
                && requestCode != REQ_PICK_SND) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            status.setText("Pick cancelled.");
            return;
        }
        String name = (requestCode == REQ_PICK_BITM) ? "bitmaps.map"
                    : (requestCode == REQ_PICK_SND)  ? "sounds.map"
                    : "bloodgulch.map";
        Uri uri = data.getData();
        File dest = new File(destDir(), name);
        File tmp = new File(destDir(), name + ".part");
        status.setText("Copying " + name + "\u2026");
        try {
            destDir().mkdirs();
            copyUri(uri, tmp);
            if (dest.exists() && !dest.delete()) {
                throw new java.io.IOException("could not replace existing file");
            }
            if (!tmp.renameTo(dest)) {
                throw new java.io.IOException("could not finalize copy");
            }
            Log.i(TAG, "[assets] copied " + dest.getAbsolutePath()
                    + " (" + dest.length() + " bytes)");
            refresh();
        } catch (Exception e) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            Log.e(TAG, "[assets] copy failed", e);
            status.setText("Copy failed: " + e.getMessage());
            Toast.makeText(this, "Copy failed: " + e.getMessage(),
                    Toast.LENGTH_LONG).show();
        }
    }

    private void copyUri(Uri uri, File dest) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(dest)) {
            if (in == null) throw new java.io.IOException("could not open selected file");
            byte[] buf = new byte[256 * 1024];
            long total = 0;
            int n;
            while ((n = in.read(buf)) >= 0) {
                out.write(buf, 0, n);
                total += n;
            }
            out.flush();
            if (total < 1024) {
                throw new java.io.IOException("file too small (" + total + " bytes)");
            }
        }
    }

    private void launchGame() {
        if (existingMap() == null) {
            status.setText("Pick a map first.");
            return;
        }
        Intent i = new Intent(this, GameActivity.class);
        startActivity(i);
        finish();
    }

    private TextView tv(String text, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER_HORIZONTAL);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private Button btn(String text, int bg) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setBackgroundColor(bg);
        b.setPadding(dp(16), dp(14), dp(16), dp(14));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        b.setLayoutParams(lp);
        return b;
    }

    private View space(int dps) {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(dps)));
        return v;
    }

    private int dp(int dps) {
        return Math.round(dps * getResources().getDisplayMetrics().density);
    }
}
