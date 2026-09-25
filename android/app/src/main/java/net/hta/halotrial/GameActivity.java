package net.hta.halotrial;

import android.app.NativeActivity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Bitmap;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.view.DisplayCutout;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;
import android.text.InputType;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.util.Enumeration;
import java.net.InetAddress;

/**
 * NativeActivity plus a COD-Mobile-style touch HUD: visible stick, a large
 * fire button, and jump. Look is anywhere that isn't a button.
 *
 * The game runs fullscreen: status bar and navigation bar hidden, and the
 * surface extended under the display cutout. A swipe from an edge brings the
 * bars back transiently, then they hide again.
 */
public class GameActivity extends NativeActivity {
    /* Where the cover ends in res/drawable-nodpi/megamod_menu.jpg (2376 wide,
     * the art 118..1313). */
    private static final float MENU_ART_RIGHT = 0.553f;
    static {
        System.loadLibrary("hta_native");
    }

    private HudOverlay hud;
    private boolean exploreExternal;
    private final ShellMenu shell = new ShellMenu(this);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // MEGAMOD SHOWDOWN's title art, handed to the native menu before its
        // thread starts (NativeActivity starts it in super.onCreate).
        try {
            System.loadLibrary("hta_native");
            android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
            o.inScaled = false;
            android.graphics.Bitmap art = android.graphics.BitmapFactory.decodeResource(getResources(),
                    R.drawable.megamod_menu, o);
            if (art != null) {
                int[] px = new int[art.getWidth() * art.getHeight()];
                art.getPixels(px, 0, art.getWidth(), 0, 0, art.getWidth(), art.getHeight());
                nativeSetMenuArt(px, art.getWidth(), art.getHeight(), MENU_ART_RIGHT);
                art.recycle();
            }
        } catch (Throwable t) {
            android.util.Log.w("hta", "menu art: " + t);
        }
        super.onCreate(savedInstanceState);
        // Diagnostics: record Java crashes, and send what the last run left.
        Report.installJavaCrashHandler(this);
        Report.sendPendingCrash(this);
        exploreExternal = getIntent().getIntExtra("explore_external", 0) != 0;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // The volume keys move the media volume, which is what the game
        // plays on, even when nothing is sounding at that moment.
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        goFullscreen();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            /* Transient bars come back after a system gesture, a notification
             * shade pull, or returning from the recents screen. Re-hide every
             * time focus returns, or the game ends up letterboxed again. */
            goFullscreen();
            getWindow().getDecorView().post(this::attachHud);
        }
    }

    /** Status bar and nav bar hidden; surface extended under the cutout. */
    private void goFullscreen() {
        Window w = getWindow();
        if (w == null) return;

        /* Let the surface reach the short edges, under the camera hole.
         * Without this the renderer is letterboxed away from the cutout and
         * the HUD has a dead strip down one side. */
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams a = w.getAttributes();
            a.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(a);
        }

        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController ctl = w.getInsetsController();
            if (ctl != null) {
                ctl.hide(WindowInsets.Type.systemBars());
                ctl.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                  | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    @Override
    protected void onDestroy() {
        detachHud();
        super.onDestroy();
        if (returnToMenu) {
            Intent i = new Intent(getApplicationContext(), SetupActivity.class);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            getApplicationContext().startActivity(i);
        }
    }

    private void attachHud() {
        if (hud != null || getWindow() == null || getWindow().getDecorView().getWindowToken() == null)
            return;
        hud = new HudOverlay(this);
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                /* No FLAG_LAYOUT_INSET_DECOR: that insets the overlay by the
                 * system bars, which would park the HUD inside a frame the
                 * game no longer has. */
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
        lp.token = getWindow().getDecorView().getWindowToken();
        lp.setTitle("hta-hud");
        if (Build.VERSION.SDK_INT >= 28) {
            lp.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        try {
            getWindowManager().addView(hud, lp);
            nativeHudReady(true);
        } catch (RuntimeException e) {
            hud = null;
            nativeHudReady(false);
        }
    }

    private void detachHud() {
        if (hud == null) return;
        try { getWindowManager().removeView(hud); } catch (RuntimeException ignored) {}
        hud = null;
        nativeHudReady(false);
    }

    static native void nativeHudReady(boolean ready);
    static native void nativeHudMove(float x, float y);
    static native void nativeHudLook(float dx, float dy);
    static native void nativeHudJump(boolean down);
    static native void nativeHudFire(boolean down);
    static native void nativeHudCrouch(boolean down);
    static native void nativeHudReload();
    static native void nativeHudMelee();
    static native void nativeHudSwap();
    static native void nativeHudZoom();
    static native void nativeHudGrenade();
    /* Debug actions, by number rather than one entry point each, so adding
     * the next one is a case in the switch and nothing else.
     *   0  hand over the next weapon in the cache's roster
     */
    static native String nativeDebugText();
    static native String nativeAmmoText();
    static native int nativeVehicleMode();
    static native String nativeVehicleText();
    static native void nativeHudAlt(boolean down);
    static native int nativeDamageFlash();
    /* The HUD's buttons that mean something now (see platform nativeHudCaps). */
    static native int nativeHudCaps();
    static native void nativeHudFly();
    static native void nativeHudAbility();
    /* The character's ability: 0..1 charging, 1 ready; its name ("LASER"). */
    static native float nativeAbilityCharge();
    static native String nativeAbilityName();
    /* Damage you dealt, floating up: x, y (0..1 of the view), amount, opacity. */
    static native float[] nativeDamageNumbers();
    /* The main menu's title art (ARGB) and where the art ends (fraction of
     * its width); the menu words sit in the band to its right. */
    static native void nativeSetMenuArt(int[] argb, int w, int h, float artRight);
    /* The first menu slot over title art: x0 y0 x1 y1 (0..1), selected; null without art. */
    static native float[] nativeMenuSoloRect();
    /* The killcam's still frame: "name<TAB>weapon<TAB>health%", "" when none. */
    static native String nativeKillcam();
    static native int nativeNetStatus();
    /* The native half of a diagnostics report, as JSON (see Report.java). */
    static native String nativeReport();
    /* Banner, place, kill feed and scoreboard, separated by 0x1E. */
    static native String nativeGameText();
    /* 1 while the main menu is up: the overlay draws no controls and hands
     * touches to the menu instead. */
    static native int nativeMenuMode();
    static native int nativePaused();
    static native void nativeResume();
    static native void nativePause();

    private boolean returnToMenu;

    /** Pause screen: leave this game for the main menu. The new activity
     *  starts only after this one's native thread has exited, so two game
     *  threads never share the native state. */
    void backToMenu() {
        returnToMenu = true;
        finish();
    }
    static native void nativeMenuTouch(int action, float x, float y);
    static native String nativeShellText();
    static native int[] nativeShellArt(int which);
    static native void nativeShellScreen(int screen);
    static native void nativeShellSound(int which);
    static native void nativeStartMatch(int[] config, String host, String name, String map);
    /* 1: held out of a custom game until a class is picked; 2: the match has classes. */
    static native int nativeClassState();
    static native void nativeChooseTeam(int team);
    static native void nativeChooseClass(String character, String primary, String secondary);
    /* The class screen's preview: a character (package id, "" the Spartan)
     * holding a weapon, drawn instead of the menu or world while on. */
    static native void nativeSetPreview(String character, String weapon, int on);
    /* Weapons a class may hold and imported bodies: "W\tname" and
     * "C\tid\tname" lines, once the menu has loaded. */
    static native String nativeCatalog();
    static native boolean nativeCharacterAvailable(String character);
    static native void nativeSetLoadout(String character, int botsImported, int classes,
                                        String primary, String secondary);
    static native String nativeLanScan(String targets, int port, int milliseconds);

    public void openSolo() { runOnUiThread(() -> { shell.open(2); if (hud != null) hud.invalidate(); }); }
    public void openMultiplayer() { runOnUiThread(() -> { shell.open(1); if (hud != null) hud.invalidate(); }); }

    static volatile boolean creditsUp;

    /** Called from native when SETTINGS is chosen. */
    public void openSettings() {
        runOnUiThread(() -> {
            Intent i = new Intent(this, SetupActivity.class);
            i.putExtra("settings", true);
            startActivity(i);
            finish();
        });
    }

    /** Called from native when CREDITS is chosen. */
    public void showCredits() {
        creditsUp = true;
    }

    private static final class ShellMenu {
        private final GameActivity owner;
        private final Paint panel = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint row = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint title = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint soloWord = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Bitmap[] art = new Bitmap[16];
        private final boolean[] artRead = new boolean[16];
        private String[] words;
        private int screen;
        private int bots = 3, skill = 1, kills = 25, minutes = 0, respawn = 5;
        /* hta_game_mode: free-for-all Slayer, Team Slayer, CTF. Solo only
         * until the LAN snapshot carries teams and flags. */
        private int gametype = 0, captures = 3;
        /* hta_game_mode. Every game is played with classes: nothing here is
         * just Halo any more. */
        private static final String[] GAMETYPES = { "SLAYER", "TEAM SLAYER", "CAPTURE THE FLAG" };
        /* Seconds nobody can hurt you after a spawn, or until you fire. */
        private int protect = 3;
        private boolean duplicateHeroes;
        /* The class picker, over the game: before your first spawn in a
         * custom game (native holds you out), or from the pause screen. */
        boolean pickerOpen;
        /* HTA_VROSTER_*: none, the map's defaults, all, or one kind. */
        private int vehicles = 2;
        private static final String[] VEHICLE_SETS = { "NONE", "DEFAULT", "ALL",
                "WARTHOGS", "GHOSTS", "SCORPIONS", "ROCKET WARTHOGS", "BANSHEES" };
        /* Worlds to play in: Blood Gulch, every imported map built into
         * this APK (assets/maps/<name>.oalmap), and the one picked in
         * Settings ("imported"). The native side loads by these names. */
        private final List<String> maps = new ArrayList<>();
        private int map = 0;
        /* Classes: a character and two weapons (by the names the game
         * shows). Every character brings a preset -- its own default loadout
         * -- and the player builds custom ones (Goku with an AK-47). */
        static final class Loadout {
            String character, primary, secondary;   // character: package id, "" the Spartan
            boolean preset;
            Loadout(String c, String p, String s, boolean preset) {
                character = c; primary = p; secondary = s; this.preset = preset;
            }
        }
        private final List<String> classWeapons = new ArrayList<>();
        private final List<String[]> characters = new ArrayList<>();   // {id, name}
        private final HashMap<String, String> heroGroups = new HashMap<>();
        private final HashMap<String, String> heroPowers = new HashMap<>();
        private final List<Loadout> presets = new ArrayList<>();
        private int pick = 0;             // into presets then customs
        private int classPage;
        private int classEdit = -1; // retained preview state
        private boolean classesLoaded;
        private int classReturn = 2;
        private boolean previewOn;
        private final List<String> classLabels = new ArrayList<>();
        private final List<Runnable> classActions = new ArrayList<>();
        private final List<Integer> classKinds = new ArrayList<>();   // 0 row, 1 selected, 2 go
        /* The rows of the match screens, rebuilt each time they are asked
         * for, and what tapping each does. */
        private final List<String> matchLabels = new ArrayList<>();
        private final List<Runnable> matchActions = new ArrayList<>();
        private int maxPlayers = 8, port = 32270;
        private String serverName = "Megamod Showdown", address = "";
        private String[] lanGames = new String[0];
        private boolean scanning;
        private Bitmap cover;

        ShellMenu(GameActivity owner) {
            this.owner = owner;
            panel.setColor(0xF0141720);
            row.setColor(0xF0272B35);
            title.setColor(0xFFFFE5BE);
            title.setTypeface(Typeface.DEFAULT_BOLD);
            title.setTextAlign(Paint.Align.CENTER);
            text.setColor(0xFFFFFFFF);
            text.setTextAlign(Paint.Align.CENTER);
            soloWord.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
            soloWord.setTextAlign(Paint.Align.CENTER);
            soloWord.setTextScaleX(1.25f);
            soloWord.setLetterSpacing(0.08f);
        }

        /* Built on first use, not in the constructor: this object is a field
         * of the activity, created before it has a context to ask. */
        private void findMaps() {
            if (!maps.isEmpty()) return;
            maps.add("bloodgulch");
            try {
                String[] bundled = owner.getAssets().list("maps");
                if (bundled != null) for (String f : bundled)
                    if (f.endsWith(".oalmap")) {
                        String id = f.substring(0, f.length() - 7);
                        /* McRonalds played badly (conveyors, a glass placeholder,
                         * a dining room that does not read as an arena). */
                        if (id.equals("mcdonalds") || id.equals("mcronalds")) continue;
                        maps.add(id);
                    }
            } catch (java.io.IOException ignored) { }
            java.io.File dir = owner.getExternalFilesDir(null);
            if (dir == null) dir = owner.getFilesDir();
            java.io.File picked = new java.io.File(dir, "external.oalmap");
            if (picked.isFile() && picked.length() > 1024) maps.add("imported");
        }

        private String mapName(String id) {
            if (id.equals("bloodgulch")) return "BLOOD GULCH";
            if (id.equals("imported")) return "IMPORTED MAP";
            if (id.endsWith("_lit"))
                return id.substring(0, id.length() - 4).toUpperCase(java.util.Locale.ROOT) + " LIT";
            return id.toUpperCase(java.util.Locale.ROOT);
        }

        private void cycleMap() { map = (map + 1) % maps.size(); }

        private void findClasses() {
            if (classesLoaded) return;
            String cat = nativeCatalog();
            if (cat == null || cat.isEmpty()) return;          // not built yet: ask again later
            classesLoaded = true;
            presets.add(new Loadout("", "assault rifle", "pistol", true));   // the Spartan
            characters.add(new String[] { "", "Spartan" });
            heroGroups.put("", "HUMAN");
            for (String line : cat.split("\n")) {
                String[] f = line.split("\t", -1);
                if (f.length >= 2 && f[0].equals("W") && !classWeapons.contains(f[1])) classWeapons.add(f[1]);
                if (f.length >= 3 && f[0].equals("C")) {
                    characters.add(new String[] { f[1], f[2] });
                    if(f.length>=8) heroPowers.put(f[1],f[7]);
                    heroGroups.put(f[1], f.length >= 6 && !f[5].isEmpty() ? f[5] : "HERO");
                    String p = f.length >= 4 && !f[3].isEmpty() ? f[3] : "assault rifle";
                    String q = f.length >= 5 && !f[4].isEmpty() ? f[4] : "pistol";
                    presets.add(new Loadout(f[1], p, q, true));
                }
            }
            if (presets.size()>1) presets.remove(0); // imported heroes have authored kits
            // Keep the expanding roster browsable by role and character.
            presets.sort((a, b) -> {
                int group = Integer.compare(heroRank(heroGroups.get(a.character)),
                                            heroRank(heroGroups.get(b.character)));
                return group != 0 ? group : charName(a.character).compareToIgnoreCase(charName(b.character));
            });
            android.content.SharedPreferences prefs = owner.getSharedPreferences("hta", android.content.Context.MODE_PRIVATE);
            pick = 0;
            String saved = prefs.getString("hero_pick", "");
            for (int i = 0; i < presets.size(); i++)
                if (presets.get(i).character.equals(saved)) pick = i;
            classPage = pick / 7;
        }

        private void saveClasses() {
            android.content.SharedPreferences.Editor e = owner.getSharedPreferences("hta", android.content.Context.MODE_PRIVATE).edit();
            e.putString("hero_pick", selected().character);
            e.apply();
        }

        private Loadout loadoutAt(int i) {
            return presets.get(i);
        }

        Loadout selected() {
            findClasses();
            int n = presets.size();
            if (n == 0) return new Loadout("", "assault rifle", "pistol", true);
            pick = Math.max(0, Math.min(n - 1, pick));
            return loadoutAt(pick);
        }

        private String charName(String id) {
            for (String[] c : characters) if (c[0].equals(id)) return c[1];
            return id.isEmpty() ? "Spartan" : id;
        }

        private int heroRank(String group) {
            if ("SUPERHERO".equals(group)) return 0;
            if ("SAIYAN".equals(group)) return 1;
            if ("WIZARD".equals(group)) return 2;
            if ("HERO".equals(group)) return 3;
            return 4;
        }

        private String nextCharacter(String id) {
            if (characters.isEmpty()) return id;
            int i = 0;
            for (int k = 0; k < characters.size(); k++) if (characters.get(k)[0].equals(id)) i = k;
            return characters.get((i + 1) % characters.size())[0];
        }

        private String loadoutLabel(Loadout l) {
            return up(charName(l.character)) + "  —  " + up(l.primary) + " + " + up(l.secondary);
        }

        private String nextWeapon(String current, String other) {
            if (classWeapons.isEmpty()) return current;
            int i = classWeapons.indexOf(current);
            for (int step = 1; step <= classWeapons.size(); step++) {
                String w = classWeapons.get((Math.max(i, 0) + step) % classWeapons.size());
                if (!w.equals(other)) return w;
            }
            return current;
        }

        private String up(String t) { return t.toUpperCase(java.util.Locale.ROOT); }

        private void row(String label, Runnable action) { matchLabels.add(label); matchActions.add(action); }

        /* SINGLEPLAYER (host false) and CREATE GAME (host true). */
        private void buildMatchRows(boolean host) {
            matchLabels.clear(); matchActions.clear();
            if (host) {
                row(word(10, "SERVER NAME") + ": " + serverName, () -> edit(false));
                row(word(11, "MAX PLAYERS") + ": " + maxPlayers, () -> maxPlayers = maxPlayers == 8 ? 2 : maxPlayers + 1);
            }
            row("MAP: " + mapName(maps.get(map)), this::cycleMap);
            row("GAME: " + GAMETYPES[gametype], () -> gametype = (gametype + 1) % GAMETYPES.length);
            row("DUPLICATE HEROES: " + (duplicateHeroes ? "ON" : "OFF"), () -> duplicateHeroes = !duplicateHeroes);
            row("BOTS: " + bots, () -> bots = (bots + 1) % 8);
            row("BOT SKILL: " + skillName(), () -> skill = (skill + 1) % 4);
            row(gametype == 2 ? "CAPTURES TO WIN: " + (captures == 0 ? "NONE" : captures)
                              : word(21, "KILLS TO WIN") + " " + (kills == 0 ? "NONE" : kills), () -> {
                if (gametype == 2) captures = next(captures, new int[] { 1, 3, 5, 10, 0 });
                else kills = next(kills, new int[] { 0, 10, 25, 50, 100 });
            });
            row("TIME LIMIT: " + (minutes == 0 ? "NONE" : minutes + " MIN"),
                () -> minutes = next(minutes, new int[] { 0, 10, 15, 20, 30, 45 }));
            row(word(22, "RESPAWN TIME") + " " + respawn + " SEC", () -> respawn = next(respawn, new int[] { 2, 5, 10, 15 }));
            row("SPAWN PROTECTION: " + (protect == 0 ? "OFF" : protect + " SEC"),
                () -> protect = next(protect, new int[] { 0, 2, 3, 5 }));
            row("VEHICLES: " + VEHICLE_SETS[vehicles], () -> vehicles = (vehicles + 1) % VEHICLE_SETS.length);
            row(word(12, "START GAME"), () -> { if (host) start(1, "127.0.0.1", maps.get(map)); else start(0, "", maps.get(map)); });
            row(word(18, "BACK"), this::back);
        }

        void open(int next) {
            findMaps();
            findClasses();
            screen = next;
            nativeShellScreen(next == 2 ? 1 : 2);
            nativeShellSound(1);
            if (words == null) {
                String raw = nativeShellText();
                words = raw == null ? new String[0] : raw.split("\u001e", -1);
            }
            if (next == 5) scan();
        }

        private String word(int n, String fallback) {
            return fallback;
        }

        private Bitmap image(int n) {
            if (artRead[n]) return art[n];
            artRead[n] = true;
            int[] data = nativeShellArt(n);
            if (data == null || data.length < 3) return null;
            int w = data[0], h = data[1];
            if (w < 1 || h < 1 || (long) w * h != data.length - 2) return null;
            art[n] = Bitmap.createBitmap(data, 2, w, w, h, Bitmap.Config.ARGB_8888);
            return art[n];
        }

        private String heading() {
            switch (screen) {
            case 1: return word(46, "MULTIPLAYER");
            case 2: return "SINGLEPLAYER";
            case 3: return "CREATE GAME";
            case 4: return word(0, "JOIN GAME");
            case 5: return "LAN GAMES";
            case 7: return "CLASSES";
            default: return "INTERNET GAME";
            }
        }

        private String[] rows() {
            switch (screen) {
            case 1: return new String[] { word(1, "CREATE GAME"), word(0, "JOIN GAME"), word(18, "BACK") };
            case 2: case 3: buildMatchRows(screen == 3); return matchLabels.toArray(new String[0]);
            case 7: return new String[0];   // drawn by drawClassUi
            case 4: return new String[] { word(3, "LAN"), word(2, "INTERNET") + " / DIRECT IP", word(18, "BACK") };
            case 5: {
                int count = Math.min(5, lanGames.length);
                String[] r = new String[count + 2];
                for (int i = 0; i < count; i++) {
                    String[] f = lanGames[i].split("\t", -1);
                    r[i] = f.length >= 5 ? f[2] + "  " + f[3] + "/" + f[4] +
                            (f.length >= 8 ? "  " + mapName(f[7]) : "") +
                            (gameFull(f) ? "  FULL" : "  " + f[0]) : lanGames[i];
                }
                r[count] = scanning ? "SCANNING..." : word(30, "REFRESH");
                r[count + 1] = word(18, "BACK");
                return r;
            }
            default: return new String[] { word(16, "SERVER ADDRESS") + ": " +
                    (address.isEmpty() ? "TAP TO ENTER" : address), word(31, "JOIN GAME"), word(18, "BACK") };
            }
        }

        private String skillName() {
            String[] fallback = { "CASUAL", "STANDARD", "AGGRESSIVE", "BRUTAL" };
            return word(26 + skill, fallback[skill]);
        }

        private int menuColumns() { return screen == 2 || screen == 3 ? 2 : 1; }
        private int menuRows(int count) { return (count + menuColumns() - 1) / menuColumns(); }
        private float menuStep(int count) { return 0.65f / Math.max(4, menuRows(count)); }

        private void ensureCover() {
            if (cover != null) return;
            try {
                android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
                o.inScaled = false;
                cover = android.graphics.BitmapFactory.decodeResource(owner.getResources(),
                        R.drawable.megamod_menu, o);
            } catch (Throwable ignored) { }
        }

        void draw(Canvas c, int w, int h) {
            if (screen == 0) return;
            String[] r = rows();
            float scale = Math.min(w, h);
            ensureCover();
            if (cover != null) {
                float aspect = (float) cover.getWidth() / Math.max(1, cover.getHeight());
                float dw = h * aspect;
                int left = (int) ((w - dw) * 0.5f);
                c.drawBitmap(cover, null, new Rect(left, 0, left + (int) dw, h), null);
                c.drawColor(0xC0100C0A);
            } else {
                c.drawColor(0xFF0B0D13);
            }
            panel.setColor(0xE6141923);
            c.drawRoundRect(w * .055f, h * .055f, w * .945f, h * .945f, 24, 24, panel);
            row.setColor(0xFFFF752B);
            c.drawRect(w * .055f, h * .055f, w * .062f, h * .945f, row);
            title.setTextSize(scale * .032f);
            c.drawText("MEGAMOD SHOWDOWN", w * .5f, h * .12f, title);
            title.setTextSize(scale * .065f);
            c.drawText(heading(), w * .5f, h * .205f, title);
            int cols = menuColumns(), nr = menuRows(r.length);
            float step = menuStep(r.length), width = .82f / cols;
            for (int i = 0; i < r.length; i++) {
                float x = .09f + (i / nr) * width, y = .255f + (i % nr) * step;
                boolean go = r[i].equals("START GAME") || r[i].equals("JOIN GAME");
                row.setColor(go ? 0xFFB64A19 : 0xFF252C39);
                c.drawRoundRect(w*x, h*y, w*(x+width-.018f), h*(y+step*.86f), 12, 12, row);
                text.setTextSize(Math.min(scale*.037f, h*step*.43f));
                float max = w*(width-.05f);
                if (text.measureText(r[i]) > max) text.setTextSize(text.getTextSize()*max/text.measureText(r[i]));
                c.drawText(r[i], w*(x+(width-.018f)*.5f), h*(y+step*.56f), text);
            }
            row.setColor(0xF0272B35);
        }

        /* The class screen: every class on the left (character presets,
         * then the player's own), the selected one's character turning on
         * the right (native draws it: nativeSetPreview). An editor for a
         * custom class takes the same place. mode 0: from match setup;
         * 1: before your first spawn (SPAWN); 2: from the pause screen,
         * for your next spawn (DONE). */
        private static final float CLS_L = 0.02f, CLS_R = 0.56f, CLS_TOP = 0.19f, CLS_BOTTOM = 0.95f;

        private void classRow(String label, int kind, Runnable action) {
            classLabels.add(label); classKinds.add(kind); classActions.add(action);
        }

        private void buildClassRows(int mode) {
            classLabels.clear(); classKinds.clear(); classActions.clear();
            findClasses();
            if ((nativeClassState() & 4) != 0) {
                classRow("JOIN RED TEAM", 1, () -> nativeChooseTeam(0));
                classRow("JOIN BLUE TEAM", 0, () -> nativeChooseTeam(1));
                return;
            }
            int total = presets.size();
            int pages = Math.max(1, (total + 6) / 7);
            classPage = Math.max(0, Math.min(classPage, pages - 1));
            for (int i = classPage * 7; i < Math.min(total, (classPage + 1) * 7); i++) {
                final int k = i;
                Loadout l = loadoutAt(i);
                String tag = up(heroGroups.getOrDefault(l.character, "HERO")) + ": ";
                String label = tag + up(charName(l.character));
                if (!nativeCharacterAvailable(l.character)) label += "  [TAKEN]";
                classRow(label, i == pick ? 1 : 0, () -> pick = k);
            }
            if (pages > 1) {
                classRow("◀ PAGE " + (classPage + 1) + "/" + pages, 0,
                         () -> classPage = (classPage + pages - 1) % pages);
                classRow("PAGE " + (classPage + 1) + "/" + pages + " ▶", 0,
                         () -> classPage = (classPage + 1) % pages);
            }
            if (mode == 0) classRow(word(18, "BACK"), 2, () -> open(classReturn));
            else classRow(mode == 1 ? "SPAWN" : "DONE", 2, () -> {
                Loadout l = selected();
                if (!nativeCharacterAvailable(l.character)) {
                    new AlertDialog.Builder(owner).setMessage(charName(l.character) +
                            " is already in this match. Pick another hero or enable Duplicate Heroes in match settings.")
                            .setPositiveButton("OK", null).show();
                    return;
                }
                nativeChooseClass(l.character, l.primary, l.secondary);
                pickerOpen = false;
                if (mode == 2) nativeResume();
            });
        }

        /* Which class screen is up tells native to draw its preview. */
        void syncPreview(boolean visible) {
            if (visible) {
                Loadout l = selected();
                nativeSetPreview(l.character, l.primary, 1);
                previewOn = true;
            } else if (previewOn) {
                nativeSetPreview("", "", 0);
                previewOn = false;
            }
        }

        void drawClassUi(Canvas c, int w, int h, int mode) {
            buildClassRows(mode);
            float scale = Math.min(w, h);
            float left = w * CLS_L, right = w * CLS_R;
            c.drawRoundRect(left, h * 0.04f, right, h * 0.97f, 18f, 18f, panel);
            title.setTextSize(scale * 0.058f);
            boolean team = (nativeClassState() & 4) != 0;
            String head = team ? "CHOOSE YOUR TEAM" : mode == 1 ? "CHOOSE YOUR CHARACTER" : "CHANGE CHARACTER";
            c.drawText(head, (left + right) * 0.5f, h * 0.12f, title);
            if (mode == 2 && classEdit < 0) {
                text.setTextSize(scale * 0.026f);
                c.drawText("TAKES EFFECT WHEN YOU NEXT SPAWN", (left + right) * 0.5f, h * 0.165f, text);
            }
            int n = classLabels.size();
            float step = Math.min(0.085f, (CLS_BOTTOM - CLS_TOP) / Math.max(1, n));
            float size = Math.min(scale * 0.036f, h * step * 0.5f);
            for (int i = 0; i < n; i++) {
                float y = h * (CLS_TOP + i * step);
                int kind = classKinds.get(i);
                int saved = row.getColor();
                if (team) row.setColor(i==0 ? 0xFFAF3444 : 0xFF2466B3);
                else if (kind == 1) row.setColor(0xE0C0501A);          // selected: the art's orange
                else if (kind == 2) row.setColor(0xE02A7A3A);
                c.drawRoundRect(left + 12, y, right - 12, y + h * step * 0.86f, 8, 8, row);
                row.setColor(saved);
                text.setTextSize(size);
                String t = classLabels.get(i);
                float maxw = right - left - 40;
                if (text.measureText(t) > maxw) text.setTextSize(size * maxw / text.measureText(t));
                c.drawText(t, (left + right) * 0.5f, y + h * step * 0.58f, text);
            }
            if (team) return;
            /* Under the preview: whose it is and what they carry. */
            Loadout l = selected();
            float px = w * 0.78f;
            text.setTextSize(scale * .033f);
            c.drawText(heroPowers.getOrDefault(l.character,""), px, h*.85f,text);
            title.setTextSize(scale * 0.05f);
            c.drawText(up(charName(l.character)), px, h * 0.90f, title);
            text.setTextSize(scale * 0.03f);
            c.drawText(up(l.primary) + "  +  " + up(l.secondary), px, h * 0.95f, text);
            if (l.character.isEmpty()) {
                text.setTextSize(scale * 0.028f);
                c.drawText("ARMORED SOLDIER", px, h * 0.5f, text);
            }
        }

        /* x, y as fractions of the view. */
        void tapClassUi(float x, float y, int mode) {
            if (x < CLS_L || x > CLS_R) return;
            buildClassRows(mode);
            int n = classLabels.size();
            float step = Math.min(0.085f, (CLS_BOTTOM - CLS_TOP) / Math.max(1, n));
            int i = (int) ((y - CLS_TOP) / step);
            if (y < CLS_TOP || i < 0 || i >= n || y > CLS_TOP + (i + 0.86f) * step) return;
            nativeShellSound(1);
            classActions.get(i).run();
            saveClasses();
        }

        void drawMainSolo(Canvas c, int w, int h) {
            // The Trial calls this slot CAMPAIGN. Until campaign maps work,
            // its action is a configurable solo Slayer match.
            float[] r = nativeMenuSoloRect();
            if (r != null && r.length >= 5) {
                // Over MEGAMOD's title art: the word in the menu's own
                // style -- wide caps, cream, fire-orange when selected.
                float cx = (r[0] + r[2]) * 0.5f * w, cy = (r[1] + r[3]) * 0.5f * h;
                soloWord.setTextSize((r[3] - r[1]) * h * 0.62f);
                boolean sel = r[4] > 0.5f;
                soloWord.setColor(sel ? 0xFFFF851A : 0xE6FFEBCC);
                soloWord.setShadowLayer(sel ? soloWord.getTextSize() * 0.35f : 0f, 0, 0, 0xCCFF6A00);
                Paint.FontMetrics fm = soloWord.getFontMetrics();
                c.drawText("SINGLEPLAYER", cx, cy - (fm.ascent + fm.descent) * 0.5f, soloWord);
                return;
            }
            row.setColor(0xE0193457);
            c.drawRoundRect(w * 0.35f, h * 0.515f, w * 0.65f, h * 0.583f,
                    8, 8, row);
            row.setColor(0xF0272B35);
            title.setTextSize(Math.min(w, h) * 0.055f);
            c.drawText("SINGLEPLAYER", w * 0.5f, h * 0.563f, title);
        }

        void tap(float x, float y) {
            if (screen == 7) { tapClassUi(x, y, 0); if (owner.hud != null) owner.hud.invalidate(); return; }
            if (screen == 0 || x < .09f || x > .91f) return;
            String[] r = rows();
            int cols = menuColumns(), nr = menuRows(r.length);
            float step = menuStep(r.length), width = .82f / cols;
            int col = Math.min(cols-1, (int)((x-.09f)/width));
            int ri = (int)((y-.255f)/step), i = col*nr+ri;
            if (y < .255f || ri < 0 || ri >= nr || i >= r.length || y > .255f+(ri+.86f)*step) return;
            nativeShellSound(1);
            switch (screen) {
            case 1:
                if (i == 0) open(3); else if (i == 1) open(4); else back();
                break;
            case 2: case 3:
                buildMatchRows(screen == 3);
                if (i < matchActions.size()) matchActions.get(i).run();
                break;
            case 4:
                if (i == 0) open(5); else if (i == 1) open(6); else back();
                break;
            case 5:
                if (i < r.length - 2) {
                    String[] f = lanGames[i].split("\t", -1);
                    if (f.length >= 2) {
                        if (gameFull(f)) {
                            new AlertDialog.Builder(owner).setMessage("This game is full. Refresh to find an open game.")
                                    .setPositiveButton("OK", null).show();
                            break;
                        }
                        try { port = Integer.parseInt(f[1]); } catch (NumberFormatException ignored) { port = 32270; }
                        joinOn(f[0], f.length >= 8 ? f[7] : "bloodgulch");
                    }
                } else if (i == r.length - 2) scan(); else back();
                break;
            case 6:
                if (i == 0) edit(true);
                else if (i == 1) {
                    if (validIPv4(address)) askAndJoin(address);
                    else new AlertDialog.Builder(owner).setMessage("Enter a valid server IPv4 address.")
                            .setPositiveButton("OK", null).show();
                }
                else if (i == 2) back();
                break;
            default: break;
            }
            if (owner.hud != null) owner.hud.invalidate();
        }

        private int next(int current, int[] values) {
            for (int i = 0; i < values.length; i++)
                if (values[i] == current) return values[(i + 1) % values.length];
            return values[0];
        }

        private boolean gameFull(String[] fields) {
            if (fields.length < 5) return false;
            try { return Integer.parseInt(fields[3]) >= Integer.parseInt(fields[4]); }
            catch (NumberFormatException ignored) { return false; }
        }

        private boolean validIPv4(String value) {
            String[] parts = value.split("\\.", -1);
            if (parts.length != 4) return false;
            for (String part : parts) {
                if (part.isEmpty() || part.length() > 3) return false;
                for (int k = 0; k < part.length(); k++)
                    if (part.charAt(k) < '0' || part.charAt(k) > '9') return false;
                if (Integer.parseInt(part) > 255) return false;
            }
            return true;
        }

        private void start(int mode, String host, String world) {
            pickerOpen = false;
            nativeSetLoadout("", 1, mode != 2 ? 1 : 0, "assault rifle", "pistol");
            // A joiner plays whatever the host chose; the host's GAME says.
            int type = mode == 2 ? 0 : Math.min(gametype, 2);
            nativeStartMatch(new int[] { mode, bots, skill, type == 2 ? captures : kills, minutes, respawn,
                    maxPlayers, port, vehicles, type, protect, duplicateHeroes ? 1 : 0 }, host, serverName, world);
            screen = 0;
        }

        /* A joiner stands in the host's world, so it must have it too. */
        private void joinOn(String host, String world) {
            if (!maps.contains(world)) {
                new AlertDialog.Builder(owner).setMessage("This game is on " + mapName(world) +
                        ", which this APK does not have.").setPositiveButton("OK", null).show();
                return;
            }
            start(2, host, world);
        }

        /* A typed address: ask that host which world it is playing first. */
        private void askAndJoin(String host) {
            new Thread(() -> {
                String result = nativeLanScan(host, port, 1200);
                String world = "bloodgulch";
                if (result != null) for (String line : result.split("\n")) {
                    String[] f = line.split("\t", -1);
                    if (f.length >= 8 && f[0].equals(host)) world = f[7];
                }
                String w = world;
                owner.runOnUiThread(() -> joinOn(host, w));
            }, "halo-ask-host").start();
        }

        private void back() {
            nativeShellSound(2);
            if (screen == 1 || screen == 2) { screen = 0; nativeShellScreen(0); }
            else if (screen == 3 || screen == 4) open(1);
            else if (screen == 7) { if (classEdit >= 0) classEdit = -1; else open(classReturn); }
            else open(4);
        }

        private void edit(boolean ip) {
            EditText input = new EditText(owner);
            input.setSingleLine(true);
            input.setInputType(ip ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI
                    : InputType.TYPE_CLASS_TEXT);
            input.setText(ip ? address : serverName);
            new AlertDialog.Builder(owner).setTitle(ip ? "Server IPv4 address" : "Server name")
                    .setView(input).setPositiveButton("OK", (d, which) -> {
                        String value = input.getText().toString().trim();
                        if (ip) address = value; else if (!value.isEmpty()) serverName = value;
                        if (owner.hud != null) owner.hud.invalidate();
                    }).setNegativeButton("Cancel", null).show();
        }

        private void scan() {
            if (scanning) return;
            scanning = true;
            lanGames = new String[0];
            new Thread(() -> {
                StringBuilder targets = new StringBuilder("255.255.255.255");
                try {
                    Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
                    while (interfaces != null && interfaces.hasMoreElements()) {
                        NetworkInterface iface = interfaces.nextElement();
                        if (!iface.isUp() || iface.isLoopback()) continue;
                        for (InterfaceAddress ia : iface.getInterfaceAddresses()) {
                            InetAddress broadcast = ia.getBroadcast();
                            if (broadcast != null) targets.append(',').append(broadcast.getHostAddress());
                        }
                    }
                } catch (Exception ignored) { }
                String result = nativeLanScan(targets.toString(), 32270, 1500);
                owner.runOnUiThread(() -> {
                    lanGames = result == null || result.isEmpty() ? new String[0] : result.split("\n");
                    scanning = false;
                    if (owner.hud != null) owner.hud.invalidate();
                });
            }, "halo-lan-scan").start();
        }
    }

    private static final class HudOverlay extends View {
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint thumb = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fireP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint jumpP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint crouchP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint reloadP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint meleeP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint swapP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint zoomP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint debug = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint ammo = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint banner = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint feed = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint board = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint boardBg = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint damage = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint waypoint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint waypointText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.Path marker = new android.graphics.Path();

        private float stickCx, stickCy, stickR, stickTx, stickTy;
        private float fireCx, fireCy, fireR;
        private float jumpCx, jumpCy, jumpR;
        private float crouchCx, crouchCy, crouchR;
        private float reloadCx, reloadCy, reloadR;
        private float meleeCx, meleeCy, meleeR;
        private float swapCx, swapCy, swapR;
        private float zoomCx, zoomCy, zoomR;
        private float nadeCx, nadeCy, nadeR;
        /* A debug pad on the left edge, clear of the stick below it and the
         * readout above it. Deliberately small and dull -- it is not part of
         * the game. */
        private float pauseCx, pauseCy, pauseR;
        private float flyCx, flyCy, flyR, abilCx, abilCy, abilR;
        private int stickPtr = -1, firePtr = -1, jumpPtr = -1, crouchPtr = -1;
        private int reloadPtr = -1, meleePtr = -1, swapPtr = -1, zoomPtr = -1;
        private int nadePtr = -1;
        private int caps;   /* nativeHudCaps at the last touch */
        private final float[] lastX = new float[16];
        private final float[] lastY = new float[16];

        private final GameActivity owner;
        private final Paint pauseBtn = new Paint(Paint.ANTI_ALIAS_FLAG);
        /* Damage numbers: TF2's red-orange, bold, over a dark drop shadow. */
        private final Paint dmgP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint dmgShadow = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint killBg = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint killBig = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint killSmall = new Paint(Paint.ANTI_ALIAS_FLAG);

        /* ---- MEGAMOD's HUD buttons: dark glass, a white icon, a small
         * caption; the fire button ringed in the title art's orange. */
        static final int IC_FIRE = 0, IC_UP = 1, IC_DOWN = 2, IC_RELOAD = 3, IC_MELEE = 4, IC_SWAP = 5,
                IC_ZOOM = 6, IC_NADE = 7, IC_FLY = 8, IC_ABILITY = 9, IC_TEXT = 10;
        private final Paint glass = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint rim = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint icon = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint caption = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.Path ip = new android.graphics.Path();
        private final android.graphics.RectF arc = new android.graphics.RectF();

        private void initButtons() {
            glass.setColor(0x7010141A);
            rim.setStyle(Paint.Style.STROKE);
            icon.setStyle(Paint.Style.STROKE);
            icon.setStrokeCap(Paint.Cap.ROUND);
            icon.setStrokeJoin(Paint.Join.ROUND);
            icon.setColor(0xF0FFFFFF);
            caption.setColor(0xD8FFFFFF);
            caption.setTextAlign(Paint.Align.CENTER);
            caption.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
            caption.setLetterSpacing(0.06f);
        }

        /* `charge` 0..1 draws a cooldown sweep; 1 or more is ready. */
        private void button(Canvas c, float cx, float cy, float r, int ic, String cap, int accent,
                            boolean held, float charge) {
            glass.setColor(held ? 0xA0303844 : 0x7010141A);
            c.drawCircle(cx, cy, r, glass);
            rim.setStrokeWidth(Math.max(2f, r * 0.06f));
            rim.setColor(accent);
            c.drawCircle(cx, cy, r - rim.getStrokeWidth() * 0.5f, rim);
            if (charge < 1f) {
                /* Cooling down: the ring fills clockwise, the icon greyed. */
                rim.setColor(0xFFFFB14A);
                arc.set(cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3);
                c.drawArc(arc, -90f, 360f * Math.max(0f, charge), false, rim);
            }
            icon.setAlpha(charge < 1f ? 110 : 240);
            icon.setStrokeWidth(Math.max(2.5f, r * 0.09f));
            float s = r * 0.42f, iy = cap == null ? cy : cy - r * 0.12f;
            ip.reset();
            switch (ic) {
            case IC_FIRE:
                c.drawCircle(cx, iy, s * 0.62f, icon);
                c.drawLine(cx - s, iy, cx - s * 0.35f, iy, icon); c.drawLine(cx + s * 0.35f, iy, cx + s, iy, icon);
                c.drawLine(cx, iy - s, cx, iy - s * 0.35f, icon); c.drawLine(cx, iy + s * 0.35f, cx, iy + s, icon);
                break;
            case IC_UP:
                ip.moveTo(cx - s * 0.8f, iy + s * 0.35f); ip.lineTo(cx, iy - s * 0.45f); ip.lineTo(cx + s * 0.8f, iy + s * 0.35f);
                c.drawPath(ip, icon); break;
            case IC_DOWN:
                ip.moveTo(cx - s * 0.8f, iy - s * 0.35f); ip.lineTo(cx, iy + s * 0.45f); ip.lineTo(cx + s * 0.8f, iy - s * 0.35f);
                c.drawPath(ip, icon); break;
            case IC_RELOAD:
                arc.set(cx - s * 0.75f, iy - s * 0.75f, cx + s * 0.75f, iy + s * 0.75f);
                c.drawArc(arc, -60f, 290f, false, icon);
                ip.moveTo(cx + s * 0.75f * 0.5f + s * 0.1f, iy - s * 0.95f);
                ip.lineTo(cx + s * 0.75f * 0.5f, iy - s * 0.62f);
                ip.lineTo(cx + s * 0.05f, iy - s * 0.78f);
                c.drawPath(ip, icon); break;
            case IC_MELEE:
                for (int k = 0; k < 8; k++) {
                    double a = k * Math.PI / 4;
                    float in = k % 2 == 0 ? s * 0.25f : s * 0.45f, out = k % 2 == 0 ? s * 0.95f : s * 0.7f;
                    c.drawLine(cx + (float) Math.cos(a) * in, iy + (float) Math.sin(a) * in,
                               cx + (float) Math.cos(a) * out, iy + (float) Math.sin(a) * out, icon);
                }
                break;
            case IC_SWAP:
                c.drawLine(cx - s * 0.8f, iy - s * 0.3f, cx + s * 0.8f, iy - s * 0.3f, icon);
                c.drawLine(cx + s * 0.8f, iy - s * 0.3f, cx + s * 0.45f, iy - s * 0.65f, icon);
                c.drawLine(cx + s * 0.8f, iy + s * 0.3f, cx - s * 0.8f, iy + s * 0.3f, icon);
                c.drawLine(cx - s * 0.8f, iy + s * 0.3f, cx - s * 0.45f, iy + s * 0.65f, icon);
                break;
            case IC_ZOOM:
                c.drawCircle(cx - s * 0.15f, iy - s * 0.15f, s * 0.55f, icon);
                c.drawLine(cx + s * 0.25f, iy + s * 0.25f, cx + s * 0.85f, iy + s * 0.85f, icon);
                break;
            case IC_NADE:
                c.drawCircle(cx, iy + s * 0.15f, s * 0.6f, icon);
                c.drawLine(cx - s * 0.2f, iy - s * 0.45f, cx + s * 0.35f, iy - s * 0.45f, icon);
                c.drawLine(cx + s * 0.35f, iy - s * 0.45f, cx + s * 0.65f, iy - s * 0.2f, icon);
                break;
            case IC_FLY:
                ip.moveTo(cx - s, iy + s * 0.1f); ip.lineTo(cx, iy - s * 0.55f); ip.lineTo(cx + s, iy + s * 0.1f);
                ip.moveTo(cx - s * 0.6f, iy + s * 0.55f); ip.lineTo(cx, iy + s * 0.05f); ip.lineTo(cx + s * 0.6f, iy + s * 0.55f);
                c.drawPath(ip, icon); break;
            case IC_ABILITY:
                for (int k = 0; k < 5; k++) {
                    double a0 = -Math.PI / 2 + k * 2 * Math.PI / 5, a1 = a0 + Math.PI / 5;
                    float x0 = cx + (float) Math.cos(a0) * s, y0 = iy + (float) Math.sin(a0) * s;
                    float x1 = cx + (float) Math.cos(a1) * s * 0.45f, y1 = iy + (float) Math.sin(a1) * s * 0.45f;
                    if (k == 0) ip.moveTo(x0, y0); else ip.lineTo(x0, y0);
                    ip.lineTo(x1, y1);
                }
                ip.close(); c.drawPath(ip, icon); break;
            default: break;
            }
            if (cap != null) {
                caption.setTextSize(r * (ic == IC_TEXT ? 0.36f : 0.27f));
                caption.setAlpha(charge < 1f ? 140 : 216);
                c.drawText(cap, cx, ic == IC_TEXT ? cy + caption.getTextSize() * 0.35f : cy + r * 0.62f, caption);
            }
        }

        HudOverlay(GameActivity a) {
            super(a);
            owner = a;
            pauseBtn.setColor(0xCC1C3A66);
            dmgP.setColor(0xFFFF4A2A);
            dmgP.setTypeface(Typeface.DEFAULT_BOLD);
            dmgP.setTextAlign(Paint.Align.CENTER);
            dmgShadow.setColor(0xFF000000);
            dmgShadow.setTypeface(Typeface.DEFAULT_BOLD);
            dmgShadow.setTextAlign(Paint.Align.CENTER);
            killBg.setColor(0xD0201A18);
            killBig.setColor(0xFFFF6A3A);
            killBig.setTypeface(Typeface.DEFAULT_BOLD);
            killBig.setTextAlign(Paint.Align.CENTER);
            killSmall.setColor(0xFFE8DCC8);
            killSmall.setTypeface(Typeface.DEFAULT_BOLD);
            killSmall.setTextAlign(Paint.Align.CENTER);
            setClickable(true);
            initButtons();
            ring.setStyle(Paint.Style.STROKE);
            ring.setStrokeWidth(4f);
            ring.setColor(0x66FFFFFF);
            fill.setColor(0x22000000);
            thumb.setColor(0x99FFFFFF);
            fireP.setColor(0xCCE23B3B);
            jumpP.setColor(0xCC2B6CF6);
            crouchP.setColor(0xCC555C66);
            reloadP.setColor(0xCCB08420);
            meleeP.setColor(0xCC6E3BA8);
            swapP.setColor(0xCC2E7D6B);
            zoomP.setColor(0xCC3C5A8C);
            label.setColor(0xFFFFFFFF);
            ammo.setColor(0xF2FFFFFF);
            ammo.setTextAlign(Paint.Align.RIGHT);
            ammo.setTypeface(Typeface.DEFAULT_BOLD);
            debug.setColor(0xCC00FF88);
            label.setTextAlign(Paint.Align.CENTER);
            label.setTypeface(Typeface.DEFAULT_BOLD);
            /* Halo's HUD messages are its pale blue. */
            banner.setColor(0xFFB8D8FF);
            banner.setTextAlign(Paint.Align.CENTER);
            banner.setTypeface(Typeface.DEFAULT_BOLD);
            banner.setShadowLayer(4f, 0f, 2f, 0xFF000000);
            feed.setColor(0xFFDDE8F5);
            feed.setShadowLayer(3f, 0f, 1f, 0xFF000000);
            board.setColor(0xFFE6E9EF);
            board.setTypeface(Typeface.MONOSPACE);
            boardBg.setColor(0xB0101820);
            damage.setColor(0xFFFF3030);
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            float m = Math.min(w, h);
            stickR = m * 0.13f;
            stickCx = w * 0.16f;
            stickCy = h * 0.78f;
            stickTx = stickCx;
            stickTy = stickCy;
            fireR = m * 0.095f;
            fireCx = w * 0.84f;
            fireCy = h * 0.62f;
            jumpR = m * 0.07f;
            jumpCx = w * 0.91f;
            jumpCy = h * 0.84f;
            crouchR = m * 0.062f;
            crouchCx = w * 0.78f;
            crouchCy = h * 0.86f;
            reloadR = m * 0.058f;
            reloadCx = w * 0.665f;
            reloadCy = h * 0.90f;
            meleeR = m * 0.058f;
            meleeCx = w * 0.70f;
            meleeCy = h * 0.72f;
            swapR = m * 0.058f;
            swapCx = w * 0.60f;
            swapCy = h * 0.90f;
            zoomR = m * 0.058f;
            zoomCx = w * 0.625f;
            zoomCy = h * 0.72f;
            nadeR = m * 0.058f;
            nadeCx = w * 0.725f;
            nadeCy = h * 0.90f;
            flyR = m * 0.06f;
            flyCx = w * 0.935f;
            flyCy = h * 0.40f;
            abilR = m * 0.085f;
            abilCx = w * 0.755f;
            abilCy = h * 0.46f;
            pauseR = m * 0.04f;
            pauseCx = w * 0.955f;
            pauseCy = h * 0.09f;
            label.setTextSize(m * 0.032f);
            banner.setTextSize(m * 0.062f);
            feed.setTextSize(m * 0.034f);
            board.setTextSize(m * 0.038f);
            ammo.setTextSize(m * 0.085f);
            excludeFromSystemGestures();
        }

        /* With the navigation bar hidden, an edge swipe is a system gesture
         * (back / home), not a look or a stick drag. Claim the edges the
         * controls actually sit on. The platform caps how much of an edge an
         * app may take, and ignores the excess; these rects are well inside
         * that, and the top of each edge is left to the system. */
        private void excludeFromSystemGestures() {
            if (Build.VERSION.SDK_INT < 29) return;
            int w = getWidth(), h = getHeight();
            if (w <= 0 || h <= 0) return;
            List<Rect> rects = new ArrayList<>(2);
            rects.add(new Rect(0, h / 2, (int) (stickCx + stickR * 1.4f), h));
            rects.add(new Rect((int) (crouchCx - crouchR * 1.4f), h / 2, w, h));
            setSystemGestureExclusionRects(rects);
        }

        private static boolean in(float x, float y, float cx, float cy, float r) {
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= r * r;
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (GameActivity.nativeMenuMode() != 0) {
                int a = e.getActionMasked();
                if (creditsUp) {
                    if (a == MotionEvent.ACTION_UP) creditsUp = false;
                    invalidate();
                    return true;
                }
                if (owner.shell.screen != 0) {
                    if (a == MotionEvent.ACTION_UP && getWidth() > 0 && getHeight() > 0)
                        owner.shell.tap(e.getX() / getWidth(), e.getY() / getHeight());
                    return true;
                }
                int code = a == MotionEvent.ACTION_DOWN ? 0
                         : a == MotionEvent.ACTION_MOVE ? 1
                         : (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) ? 2 : -1;
                if (code >= 0 && getWidth() > 0 && getHeight() > 0)
                    GameActivity.nativeMenuTouch(code, e.getX() / getWidth(), e.getY() / getHeight());
                return true;
            }
            int classState = GameActivity.nativeClassState();
            if ((classState & 1) != 0 || (owner.shell.pickerOpen && (classState & 2) != 0)) {
                if (e.getActionMasked() == MotionEvent.ACTION_UP && getWidth() > 0 && getHeight() > 0)
                    owner.shell.tapClassUi(e.getX() / getWidth(), e.getY() / getHeight(), (classState & 1) != 0 ? 1 : 2);
                invalidate();
                return true;
            }
            if (GameActivity.nativePaused() != 0) {
                if (e.getActionMasked() == MotionEvent.ACTION_UP) {
                    float x = e.getX(), y = e.getY(), w = getWidth(), h = getHeight();
                    if (x > w * 0.35f && x < w * 0.65f) {
                        if (y > h * 0.44f && y < h * 0.56f) GameActivity.nativeResume();
                        else if (y > h * 0.60f && y < h * 0.72f) owner.backToMenu();
                        else if ((classState & 2) != 0 && y > h * 0.76f && y < h * 0.88f) owner.shell.pickerOpen = true;
                    }
                    /* SEND REPORT, top right: what this phone measured, for an agent. */
                    if (x > w * 0.72f && x < w * 0.97f && y > h * 0.04f && y < h * 0.14f)
                        Report.send(owner, "button", false);
                }
                invalidate();
                return true;
            }
            int action = e.getActionMasked();
            int idx = e.getActionIndex();
            int id = e.getPointerId(idx);
            float x = e.getX(idx), y = e.getY(idx);

            switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                caps = GameActivity.nativeHudCaps();
                remember(id, x, y);
                if (!owner.exploreExternal && in(x, y, fireCx, fireCy, fireR * 1.15f) && firePtr < 0) {
                    firePtr = id;
                    GameActivity.nativeHudFire(true);
                } else if (in(x, y, jumpCx, jumpCy, jumpR * 1.15f) && jumpPtr < 0) {
                    jumpPtr = id;
                    GameActivity.nativeHudJump(true);
                } else if (in(x, y, crouchCx, crouchCy, crouchR * 1.15f) && crouchPtr < 0) {
                    crouchPtr = id;
                    GameActivity.nativeHudCrouch(true);
                } else if ((caps & 1) != 0 && in(x, y, flyCx, flyCy, flyR * 1.2f)) {
                    GameActivity.nativeHudFly();
                } else if ((caps & 128) != 0 && in(x, y, abilCx, abilCy, abilR * 1.15f)) {
                    GameActivity.nativeHudAbility();
                } else if (!owner.exploreExternal && (caps & 8) != 0 && in(x, y, reloadCx, reloadCy, reloadR * 1.15f) && reloadPtr < 0) {
                    reloadPtr = id;
                    GameActivity.nativeHudReload();
                } else if (!owner.exploreExternal && (caps & 16) == 0 && in(x, y, meleeCx, meleeCy, meleeR * 1.15f) && meleePtr < 0) {
                    meleePtr = id;
                    GameActivity.nativeHudMelee();
                } else if (!owner.exploreExternal && in(x, y, swapCx, swapCy, swapR * 1.15f) && swapPtr < 0) {
                    swapPtr = id;
                    GameActivity.nativeHudSwap();
                } else if (!owner.exploreExternal && (caps & 4) != 0 && in(x, y, zoomCx, zoomCy, zoomR * 1.15f) && zoomPtr < 0) {
                    zoomPtr = id;
                    GameActivity.nativeHudZoom();
                } else if (in(x, y, pauseCx, pauseCy, pauseR * 1.3f)) {
                    GameActivity.nativePause();
                } else if (!owner.exploreExternal && ((caps & 32) != 0 || (GameActivity.nativeVehicleMode() & 16) != 0)
                        && in(x, y, nadeCx, nadeCy, nadeR * 1.15f) && nadePtr < 0) {
                    nadePtr = id;
                    /* In a vehicle with a second gun, NADE is that trigger,
                     * held like FIRE: the Scorpion's machine gun, the
                     * Banshee's fuel rod. */
                    nadeAlt = (GameActivity.nativeVehicleMode() & 16) != 0;
                    if (nadeAlt) GameActivity.nativeHudAlt(true);
                    else GameActivity.nativeHudGrenade();
                } else if ((in(x, y, stickCx, stickCy, stickR * 1.4f) || x < getWidth() * 0.38f)
                        && stickPtr < 0) {
                    stickPtr = id;
                    updateStick(x, y);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int pid = e.getPointerId(i);
                    float px = e.getX(i), py = e.getY(i);
                    if (pid == stickPtr) updateStick(px, py);
                    else {
                        /* Fire, jump, and empty-space drags all look so you
                         * can aim while holding FIRE (second finger or drag). */
                        float dx = px - lastOf(pid, true, px);
                        float dy = py - lastOf(pid, false, py);
                        if (dx != 0f || dy != 0f)
                            GameActivity.nativeHudLook(dx, dy);
                    }
                    remember(pid, px, py);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (action == MotionEvent.ACTION_CANCEL) {
                    releaseStick();
                    releaseFire();
                    releaseJump();
                    releaseCrouch();
                    reloadPtr = -1;
                    meleePtr = -1;
                    swapPtr = -1;
                    zoomPtr = -1;
                    releaseNade();
                } else {
                    if (id == stickPtr) releaseStick();
                    if (id == firePtr) releaseFire();
                    if (id == jumpPtr) releaseJump();
                    if (id == crouchPtr) releaseCrouch();
                    if (id == reloadPtr) reloadPtr = -1;
                    if (id == meleePtr) meleePtr = -1;
                    if (id == swapPtr) swapPtr = -1;
                    if (id == zoomPtr) zoomPtr = -1;
                    if (id == nadePtr) releaseNade();
                }
                break;
            default:
                break;
            }
            invalidate();
            return true;
        }

        private void remember(int id, float x, float y) {
            int i = ((id % 16) + 16) % 16;
            lastX[i] = x;
            lastY[i] = y;
        }

        private float lastOf(int id, boolean x, float fallback) {
            int i = ((id % 16) + 16) % 16;
            return x ? lastX[i] : lastY[i];
        }

        private void updateStick(float x, float y) {
            float dx = x - stickCx, dy = y - stickCy;
            float len = (float) Math.hypot(dx, dy);
            if (len > stickR) {
                dx *= stickR / len;
                dy *= stickR / len;
                len = stickR;
            }
            stickTx = stickCx + dx;
            stickTy = stickCy + dy;
            float nx = dx / stickR, ny = dy / stickR;
            GameActivity.nativeHudMove(nx, -ny);
        }

        private void releaseStick() {
            stickPtr = -1;
            stickTx = stickCx;
            stickTy = stickCy;
            GameActivity.nativeHudMove(0f, 0f);
        }

        private void releaseFire() {
            firePtr = -1;
            GameActivity.nativeHudFire(false);
        }

        private void releaseJump() {
            jumpPtr = -1;
            GameActivity.nativeHudJump(false);
        }

        private void releaseCrouch() {
            crouchPtr = -1;
            GameActivity.nativeHudCrouch(false);
        }

        private boolean nadeAlt;
        private void releaseNade() {
            nadePtr = -1;
            if (nadeAlt) GameActivity.nativeHudAlt(false);
            nadeAlt = false;
        }

        private void drawCredits(Canvas c) {
            int w = getWidth(), h = getHeight();
            c.drawRect(w * 0.15f, h * 0.12f, w * 0.85f, h * 0.88f, boardBg);
            String[] lines = {
                "HALO: MP",
                "",
                "Halo: Combat Evolved by Bungie; PC Trial by Gearbox Software.",
                "Every map, model, sound and word here comes from your own",
                "copy of the Trial, read at runtime.",
                "",
                "This engine: C, Vulkan and AAudio, GPLv3.",
                "Tag layouts from Invader. Ogg Vorbis by stb_vorbis.",
                "",
                "Tap to return."
            };
            float y = h * 0.22f;
            for (String l : lines) {
                c.drawText(l, w * 0.5f, y, l.startsWith("HALO") ? banner : label);
                y += label.getTextSize() * 1.9f;
            }
        }

        /* The game's words: the announcer's banner across the middle, where
         * you stand along the top, who killed whom down the left, and at the
         * end of a game the scoreboard. */
        private void drawGame(Canvas c) {
            String g = null;
            try { g = nativeGameText(); } catch (Throwable ignored) { }
            if (g == null || g.isEmpty()) return;
            String[] part = g.split("\u001e", -1);
            int w = getWidth(), h = getHeight();
            if (part.length > 0 && !part[0].isEmpty())
                c.drawText(part[0], w * 0.5f, h * 0.30f, banner);
            if (part.length > 1 && !part[1].isEmpty()) {
                float saved = banner.getTextSize();
                banner.setTextSize(feed.getTextSize());
                c.drawText(part[1], w * 0.5f, h * 0.075f, banner);
                banner.setTextSize(saved);
            }
            if (part.length > 2 && !part[2].isEmpty()) {
                String[] lines = part[2].split("\n");
                float y = h * 0.20f;
                for (String l : lines) {
                    if (l.isEmpty()) continue;
                    c.drawText(l, w * 0.09f, y, feed);
                    y += feed.getTextSize() * 1.3f;
                }
            }
            if (part.length > 4 && !part[4].isEmpty()) {
                float saved = banner.getTextSize();
                banner.setTextSize(feed.getTextSize() * 1.1f);
                c.drawText(part[4], w * 0.5f, h * 0.64f, banner);
                banner.setTextSize(saved);
            }
            if (part.length > 5 && !part[5].isEmpty()) drawWaypoints(c, part[5], w, h);
            if (part.length > 3 && !part[3].isEmpty()) {
                String[] rows = part[3].split("\n");
                float rowH = board.getTextSize() * 1.45f;
                float top = h * 0.38f, left = w * 0.22f, right = w * 0.78f;
                c.drawRect(left, top - rowH, right, top + rowH * (rows.length + 0.4f), boardBg);
                float[] col = { left + w * 0.015f, left + w * 0.07f, left + w * 0.30f,
                                left + w * 0.38f, left + w * 0.46f, left + w * 0.54f };
                String[] head = { "Place", "Name", "Score", "Kills", "Assists", "Deaths" };
                for (int k = 0; k < head.length; k++) c.drawText(head[k], col[k], top, board);
                float y = top + rowH;
                for (String r : rows) {
                    String[] f = r.split("\t");
                    for (int k = 0; k < f.length && k < col.length; k++) c.drawText(f[k], col[k], y, board);
                    y += rowH;
                }
            }
        }

        /* A flag's waypoint: a downward chevron in its team's colour over
         * where it is, the distance under it; off screen, pinned to the
         * edge. "x,y,team,metres,onscreen,state;" per flag. */
        private void drawWaypoints(Canvas c, String spec, int w, int h) {
            float size = Math.min(w, h) * 0.028f;
            waypointText.setTextAlign(Paint.Align.CENTER);
            waypointText.setTextSize(size * 1.05f);
            waypointText.setFakeBoldText(true);
            for (String one : spec.split(";")) {
                String[] f = one.split(",");
                if (f.length < 6) continue;
                float x, y; int team, state; String metres;
                try {
                    x = Float.parseFloat(f[0]) * w; y = Float.parseFloat(f[1]) * h;
                    team = Integer.parseInt(f[2]); state = Integer.parseInt(f[5]);
                    metres = f[3];
                } catch (NumberFormatException e) { continue; }
                int colour = team == 0 ? 0xFFE0402E : 0xFF3E7BFF;
                // A flag away from home blinks: somebody has it or it is lying out.
                int alpha = state == 0 ? 0xD0 : ((System.currentTimeMillis() / 300) % 2 == 0 ? 0xFF : 0x90);
                waypoint.setColor((colour & 0x00FFFFFF) | (alpha << 24));
                waypoint.setStyle(Paint.Style.FILL);
                marker.reset();
                marker.moveTo(x - size, y - size * 1.2f);
                marker.lineTo(x + size, y - size * 1.2f);
                marker.lineTo(x, y);
                marker.close();
                c.drawPath(marker, waypoint);
                waypointText.setColor(0xE0FFFFFF);
                c.drawText(metres + "m", x, y + size * 1.3f, waypointText);
            }
        }

        @Override
        protected void onDraw(Canvas c) {
            if (GameActivity.nativeMenuMode() != 0) {
                owner.shell.syncPreview(!creditsUp && owner.shell.screen == 7);
                if (creditsUp) drawCredits(c);
                else if (owner.shell.screen != 0) owner.shell.draw(c, getWidth(), getHeight());
                else owner.shell.drawMainSolo(c, getWidth(), getHeight());
                postInvalidateDelayed(100);
                return;
            }
            int classState = GameActivity.nativeClassState();
            boolean picking = (classState & 1) != 0 || (owner.shell.pickerOpen && (classState & 2) != 0);
            owner.shell.syncPreview(picking && (classState & 4) == 0);
            if (picking) {
                owner.shell.drawClassUi(c, getWidth(), getHeight(), (classState & 1) != 0 ? 1 : 2);
                postInvalidateDelayed(100);
                return;
            }
            if (GameActivity.nativePaused() != 0) {
                /* Halo's pause: the world held behind a veil, two choices
                 * (three when the match has classes). */
                int w = getWidth(), h = getHeight();
                c.drawRect(0, 0, w, h, boardBg);
                c.drawText("PAUSED", w * 0.5f, h * 0.34f, banner);
                c.drawRect(w * 0.35f, h * 0.44f, w * 0.65f, h * 0.56f, pauseBtn);
                c.drawRect(w * 0.35f, h * 0.60f, w * 0.65f, h * 0.72f, pauseBtn);
                c.drawText("RESUME GAME", w * 0.5f, h * 0.515f, label);
                c.drawText("QUIT TO MAIN MENU", w * 0.5f, h * 0.675f, label);
                c.drawRect(w * 0.72f, h * 0.04f, w * 0.97f, h * 0.14f, pauseBtn);
                c.drawText("SEND REPORT", w * 0.845f, h * 0.105f, label);
                if (!Report.lastStatus.isEmpty())
                    c.drawText(Report.lastStatus, w * 0.5f, h * 0.95f, label);
                if ((classState & 2) != 0) {
                    c.drawRect(w * 0.35f, h * 0.76f, w * 0.65f, h * 0.88f, pauseBtn);
                    c.drawText("CHANGE CHARACTER", w * 0.5f, h * 0.835f, label);
                }
                drawGame(c);
                postInvalidateDelayed(100);
                return;
            }
            int vehicleMode = GameActivity.nativeVehicleMode();
            int netStatus = GameActivity.nativeNetStatus();
            if (netStatus != 0) {
                String connection = netStatus == 1 ? "CONNECTING TO GAME..." :
                        netStatus == 2 ? "CONNECTED" :
                        netStatus == 3 ? "HOSTING · WAITING FOR PLAYER" :
                        netStatus == 4 ? "HOSTING · PLAYER JOINED" :
                        netStatus == 6 ? "WAITING FOR MATCH STATE" :
                        netStatus == 7 ? "MAPS DO NOT MATCH" :
                        netStatus == 8 ? "GAME IS FULL" : "NETWORK UNAVAILABLE";
                c.drawText(connection, getWidth() * 0.5f, getHeight() * 0.135f, label);
            }
            float[] dmg = GameActivity.nativeDamageNumbers();
            if (dmg != null && dmg.length >= 4) {
                int w = getWidth(), h = getHeight();
                dmgP.setTextSize(Math.min(w, h) * 0.055f);
                for (int i = 0; i + 3 < dmg.length; i += 4) {
                    int a = (int) (Math.max(0f, Math.min(1f, dmg[i + 3])) * 255);
                    String t = "-" + Math.max(1, Math.round(dmg[i + 2]));
                    float x = dmg[i] * w, y = dmg[i + 1] * h;
                    dmgShadow.setTextSize(dmgP.getTextSize());
                    dmgShadow.setAlpha(a * 3 / 4);
                    c.drawText(t, x + 3, y + 3, dmgShadow);
                    dmgP.setAlpha(a);
                    c.drawText(t, x, y, dmgP);
                }
            }
            String kc = GameActivity.nativeKillcam();
            if (kc != null && !kc.isEmpty()) {
                /* TF2's freeze panel: who, with what, and how hurt they were. */
                String[] f = kc.split("\t", -1);
                int w = getWidth(), h = getHeight();
                float m = Math.min(w, h);
                float bw = w * 0.42f, bh = m * 0.20f, bx = (w - bw) * 0.5f, by = h * 0.70f;
                c.drawRoundRect(bx, by, bx + bw, by + bh, 14, 14, killBg);
                killSmall.setTextSize(m * 0.035f);
                killBig.setTextSize(m * 0.065f);
                c.drawText("YOU WERE KILLED BY", w * 0.5f, by + bh * 0.28f, killSmall);
                c.drawText(f[0], w * 0.5f, by + bh * 0.62f, killBig);
                String sub = (f.length > 1 && !f[1].isEmpty() ? f[1] : "")
                        + (f.length > 2 ? (f[1].isEmpty() ? "" : "  \u00b7  ") + f[2] + "% HEALTH LEFT" : "");
                c.drawText(sub, w * 0.5f, by + bh * 0.88f, killSmall);
            }
            int hit = GameActivity.nativeDamageFlash();
            if (hit > 0) {
                int w = getWidth(), h = getHeight();
                float edge = Math.min(w, h) * 0.035f;
                damage.setStyle(Paint.Style.FILL);
                damage.setAlpha(hit * 35 / 255);
                c.drawRect(0, 0, w, h, damage);
                damage.setStyle(Paint.Style.STROKE);
                damage.setStrokeWidth(edge);
                damage.setAlpha(hit * 190 / 255);
                c.drawRect(edge * 0.5f, edge * 0.5f,
                        w - edge * 0.5f, h - edge * 0.5f, damage);
                damage.setStyle(Paint.Style.FILL);
            }
            int seatMode = vehicleMode & 15;
            if (vehicleMode != 0) {
                String vt = null;
                try { vt = nativeVehicleText(); } catch (Throwable ignored) { }
                if (vt != null && !vt.isEmpty()) {
                    float savedSize = label.getTextSize();
                    label.setTextSize(Math.min(getWidth(), getHeight()) * 0.030f);
                    c.drawText(vt, getWidth() * 0.5f, getHeight() * 0.18f, label);
                    label.setTextSize(savedSize);
                }
            }
            c.drawCircle(stickCx, stickCy, stickR, fill);
            c.drawCircle(stickCx, stickCy, stickR, ring);
            c.drawCircle(stickTx, stickTy, stickR * 0.38f, thumb);

            /* The pause button, small, top right. */
            button(c, pauseCx, pauseCy, pauseR, IC_TEXT, "II", 0x66FFFFFF, false, 1f);
            int hc = GameActivity.nativeHudCaps();
            boolean inAir = (hc & 2) != 0;
            if (!owner.exploreExternal)
                button(c, fireCx, fireCy, fireR, IC_FIRE, (hc & 16) != 0 ? "SWING" : "FIRE", 0xFFFF7A1A, firePtr >= 0, 1f);
            if (seatMode == 2) button(c, jumpCx, jumpCy, jumpR, IC_TEXT, "BRAKE", 0x88FFFFFF, jumpPtr >= 0, 1f);
            else button(c, jumpCx, jumpCy, jumpR, IC_UP, inAir ? "UP" : "JUMP", 0x88FFFFFF, jumpPtr >= 0, 1f);
            button(c, crouchCx, crouchCy, crouchR, IC_DOWN, inAir ? "DOWN" : "CROUCH", 0x88FFFFFF, crouchPtr >= 0, 1f);
            if ((hc & 1) != 0)
                button(c, flyCx, flyCy, flyR, IC_FLY, inAir ? "LAND" : "FLY", 0xFF7FD4FF, false, 1f);
            if ((hc & 128) != 0) {
                float ch = GameActivity.nativeAbilityCharge();
                String an = GameActivity.nativeAbilityName();
                button(c, abilCx, abilCy, abilR, IC_ABILITY, an == null || an.isEmpty() ? "POWER" : an,
                        0xFFFFD23A, false, ch);
            }

            if (!owner.exploreExternal) {
                if ((hc & 8) != 0) button(c, reloadCx, reloadCy, reloadR, IC_RELOAD, "RELOAD", 0x88FFFFFF, reloadPtr >= 0, 1f);
                if ((hc & 16) == 0) button(c, meleeCx, meleeCy, meleeR, IC_MELEE, "MELEE", 0x88FFFFFF, meleePtr >= 0, 1f);
                if (seatMode >= 1) button(c, swapCx, swapCy, swapR, IC_TEXT, seatMode >= 2 ? "EXIT" : "GET IN", 0xFF7FD4FF, swapPtr >= 0, 1f);
                else button(c, swapCx, swapCy, swapR, IC_SWAP, "SWAP", 0x88FFFFFF, swapPtr >= 0, 1f);
                if ((hc & 4) != 0) button(c, zoomCx, zoomCy, zoomR, IC_ZOOM, "ZOOM", 0x88FFFFFF, zoomPtr >= 0, 1f);
                if ((vehicleMode & 16) != 0) button(c, nadeCx, nadeCy, nadeR, IC_TEXT, "ALT", 0xFFFF7A1A, nadePtr >= 0, 1f);
                else if ((hc & 32) != 0) button(c, nadeCx, nadeCy, nadeR, IC_NADE, "GRENADE", 0x88FFFFFF, nadePtr >= 0, 1f);

                /* Ammo, big and bottom-right: loaded / reserve, "--" while the
                 * magazine is out. */
                String a = null;
                try { a = nativeAmmoText(); } catch (Throwable ignored) { }
                if (a != null && a.length() > 0)
                    c.drawText(a, getWidth() - 28f, getHeight() * 0.30f, ammo);

                drawGame(c);
            }

            /* Position readout, so a bug report screenshot carries coordinates.
             * Keep it clear of the camera cutout: the status bar no longer
             * covers these digits, but the punch-hole still would, and an
             * unreadable X is an unmeasurable bug report. */
            String t = null;
            try { t = nativeDebugText(); } catch (Throwable ignored) { }
            if (t != null && t.length() > 0) {
                debug.setTextSize(label.getTextSize() * 0.8f);
                float left = 24f, top = 0f;
                if (Build.VERSION.SDK_INT >= 28) {
                    WindowInsets wi = getRootWindowInsets();
                    DisplayCutout dc = (wi != null) ? wi.getDisplayCutout() : null;
                    if (dc != null) {
                        left = Math.max(left, dc.getSafeInsetLeft() + 16f);
                        top = dc.getSafeInsetTop();
                    }
                }
                c.drawText(t, left, top + debug.getTextSize() * 2.2f, debug);
            }
            postInvalidateDelayed(100);
        }
    }
}
