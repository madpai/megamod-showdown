package net.hta.halotrial;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;
import android.os.Debug;
import android.os.PowerManager;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/* SEND REPORT: everything the phone can measure, as one JSON document, for
 * an agent to read instead of a person describing it. The native half comes
 * from GameActivity.nativeReport() (frame times, video, audio, network,
 * effects, the match); this adds the build, the device, memory, thermal
 * state, the app's own recent log, and any crash record the previous run
 * left. Saved on the device (files/reports/) and, in the owner's personal
 * build, posted to the sideload server's /report (BuildConfig.REPORT_URL). */
final class Report {
    private static final String TAG = "hta-report";
    static volatile String lastStatus = "";

    private Report() {}

    /* Where crash records are written: the native signal handler writes
     * crash-native.txt here (externalDataPath), the Java handler crash-java.txt. */
    static File dataDir(Context c) {
        File d = c.getExternalFilesDir(null);
        return d != null ? d : c.getFilesDir();
    }

    /* Java crashes: record, then let Android handle it as before. */
    static void installJavaCrashHandler(Context c) {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        final File out = new File(dataDir(c), "crash-java.txt");
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try (FileOutputStream f = new FileOutputStream(out)) {
                f.write(("thread " + t.getName() + "\n" + Log.getStackTraceString(e)).getBytes(StandardCharsets.UTF_8));
            } catch (Throwable ignored) { }
            if (prev != null) prev.uncaughtException(t, e);
        });
    }

    /* A crash record from the last run, if any: send it (and clear it). */
    static void sendPendingCrash(Activity a) {
        File dir = dataDir(a);
        File n = new File(dir, "crash-native.txt"), j = new File(dir, "crash-java.txt");
        if (!n.exists() && !j.exists()) return;
        send(a, "crash", true);
    }

    /* Build, save and send on a background thread. `reason`: "button" or "crash". */
    static void send(final Activity a, final String reason, final boolean includeCrash) {
        lastStatus = "SENDING REPORT...";
        new Thread(() -> {
            try {
                JSONObject r = build(a, reason, includeCrash);
                String text = r.toString(1);
                String name = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date())
                        + "-" + reason + ".json";
                File dir = new File(dataDir(a), "reports");
                dir.mkdirs();
                File saved = new File(dir, name);
                Files.write(saved.toPath(), text.getBytes(StandardCharsets.UTF_8));
                String url = BuildConfig.REPORT_URL;
                if (url == null || url.isEmpty()) {
                    lastStatus = "REPORT SAVED ON PHONE (NO SERVER IN THIS BUILD)";
                } else {
                    int code = post(url, text, name);
                    lastStatus = code == 200 ? "REPORT SENT" : "REPORT SAVED; SEND FAILED (" + code + ")";
                }
                if (includeCrash && lastStatus.startsWith("REPORT SENT")) {
                    File dirs = dataDir(a);
                    new File(dirs, "crash-native.txt").delete();
                    new File(dirs, "crash-java.txt").delete();
                }
                Log.i(TAG, lastStatus + ": " + saved);
            } catch (Throwable t) {
                lastStatus = "REPORT FAILED: " + t.getClass().getSimpleName();
                Log.w(TAG, "report", t);
            }
        }, "hta-report").start();
    }

    static JSONObject build(Activity a, String reason, boolean includeCrash) throws Exception {
        JSONObject r = new JSONObject();
        r.put("kind", "megamod-report");
        r.put("schema", 1);
        r.put("reason", reason);
        r.put("time", new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssZ", Locale.US).format(new Date()));

        JSONObject build = new JSONObject();
        build.put("version", BuildConfig.VERSION_NAME);        // 0.2-<commit>
        build.put("code", BuildConfig.VERSION_CODE);
        build.put("personal", BuildConfig.PERSONAL);
        build.put("package", a.getPackageName());
        r.put("build", build);

        JSONObject dev = new JSONObject();
        dev.put("manufacturer", Build.MANUFACTURER);
        dev.put("model", Build.MODEL);
        dev.put("device", Build.DEVICE);
        dev.put("soc", Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "");
        dev.put("android", Build.VERSION.RELEASE);
        dev.put("sdk", Build.VERSION.SDK_INT);
        dev.put("abi", Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "");
        ActivityManager am = (ActivityManager) a.getSystemService(Context.ACTIVITY_SERVICE);
        if (am != null) {
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            dev.put("ram_total_mb", mi.totalMem / 1048576);
            dev.put("ram_avail_mb", mi.availMem / 1048576);
            dev.put("low_memory", mi.lowMemory);
        }
        dev.put("native_heap_mb", Debug.getNativeHeapAllocatedSize() / 1048576.0);
        dev.put("java_heap_mb", (Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()) / 1048576.0);
        PowerManager pm = (PowerManager) a.getSystemService(Context.POWER_SERVICE);
        if (pm != null && Build.VERSION.SDK_INT >= 29) {
            int t = pm.getCurrentThermalStatus();       // 0 none .. 6 shutdown
            String[] names = { "none", "light", "moderate", "severe", "critical", "emergency", "shutdown" };
            dev.put("thermal", t >= 0 && t < names.length ? names[t] : String.valueOf(t));
            dev.put("power_save", pm.isPowerSaveMode());
        }
        android.util.DisplayMetrics dm = a.getResources().getDisplayMetrics();
        dev.put("screen", dm.widthPixels + "x" + dm.heightPixels + " @" + dm.densityDpi + "dpi");
        r.put("device", dev);

        try {
            r.put("native", new JSONObject(GameActivity.nativeReport()));
        } catch (Throwable t) {
            r.put("native_error", String.valueOf(t));
        }

        if (includeCrash) {
            JSONObject crash = new JSONObject();
            File dir = dataDir(a);
            File n = new File(dir, "crash-native.txt"), j = new File(dir, "crash-java.txt");
            if (n.exists()) crash.put("native", new String(Files.readAllBytes(n.toPath()), StandardCharsets.UTF_8));
            if (j.exists()) crash.put("java", new String(Files.readAllBytes(j.toPath()), StandardCharsets.UTF_8));
            r.put("crash", crash);
        }
        r.put("log", logTail(600));
        return r;
    }

    /* The app's own log (any process of this app still in the buffer, so a
     * crashed previous run shows too): no permission needed for one's own. */
    static JSONArray logTail(int lines) {
        JSONArray out = new JSONArray();
        try {
            Process p = Runtime.getRuntime().exec(new String[] { "logcat", "-d", "-v", "time", "-t", String.valueOf(lines) });
            try (BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = br.readLine()) != null) out.put(line);
            }
            p.waitFor();
        } catch (Throwable t) {
            out.put("logcat unavailable: " + t);
        }
        return out;
    }

    static int post(String url, String body, String name) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(5000);
        c.setReadTimeout(10000);
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setRequestProperty("Content-Type", "application/json");
        c.setRequestProperty("X-Filename", name);
        byte[] b = body.getBytes(StandardCharsets.UTF_8);
        c.setFixedLengthStreamingMode(b.length);
        try (OutputStream o = c.getOutputStream()) { o.write(b); }
        int code = c.getResponseCode();
        c.disconnect();
        return code;
    }
}
