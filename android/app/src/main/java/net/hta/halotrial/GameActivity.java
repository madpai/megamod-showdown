package net.hta.halotrial;

import android.app.NativeActivity;
import android.graphics.Canvas;
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

import java.util.ArrayList;
import java.util.List;

/**
 * NativeActivity plus a COD-Mobile-style touch HUD: visible stick, a large
 * fire button, and jump. Look is anywhere that isn't a button.
 *
 * The game runs fullscreen: status bar and navigation bar hidden, and the
 * surface extended under the display cutout. A swipe from an edge brings the
 * bars back transiently, then they hide again.
 */
public class GameActivity extends NativeActivity {
    static {
        System.loadLibrary("hta_native");
    }

    private HudOverlay hud;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
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
    static native void nativeHudDebug(int action);
    static native String nativeDebugText();
    static native String nativeAmmoText();
    static native int nativeVehicleMode();

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
        private final Paint dbgP = new Paint(Paint.ANTI_ALIAS_FLAG);

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
        private float dbgCx, dbgCy, dbgR;
        private int stickPtr = -1, firePtr = -1, jumpPtr = -1, crouchPtr = -1;
        private int reloadPtr = -1, meleePtr = -1, swapPtr = -1, zoomPtr = -1;
        private int nadePtr = -1, dbgPtr = -1;
        private final float[] lastX = new float[16];
        private final float[] lastY = new float[16];

        HudOverlay(GameActivity a) {
            super(a);
            setClickable(true);
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
            dbgP.setColor(0x99202830);
            label.setColor(0xFFFFFFFF);
            ammo.setColor(0xF2FFFFFF);
            ammo.setTextAlign(Paint.Align.RIGHT);
            ammo.setTypeface(Typeface.DEFAULT_BOLD);
            debug.setColor(0xCC00FF88);
            label.setTextAlign(Paint.Align.CENTER);
            label.setTypeface(Typeface.DEFAULT_BOLD);
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
            dbgR = m * 0.045f;
            dbgCx = w * 0.035f;
            dbgCy = h * 0.42f;
            label.setTextSize(m * 0.032f);
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
            int action = e.getActionMasked();
            int idx = e.getActionIndex();
            int id = e.getPointerId(idx);
            float x = e.getX(idx), y = e.getY(idx);

            switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                remember(id, x, y);
                if (in(x, y, fireCx, fireCy, fireR * 1.15f) && firePtr < 0) {
                    firePtr = id;
                    GameActivity.nativeHudFire(true);
                } else if (in(x, y, jumpCx, jumpCy, jumpR * 1.15f) && jumpPtr < 0) {
                    jumpPtr = id;
                    GameActivity.nativeHudJump(true);
                } else if (in(x, y, crouchCx, crouchCy, crouchR * 1.15f) && crouchPtr < 0) {
                    crouchPtr = id;
                    GameActivity.nativeHudCrouch(true);
                } else if (in(x, y, reloadCx, reloadCy, reloadR * 1.15f) && reloadPtr < 0) {
                    reloadPtr = id;
                    GameActivity.nativeHudReload();
                } else if (in(x, y, meleeCx, meleeCy, meleeR * 1.15f) && meleePtr < 0) {
                    meleePtr = id;
                    GameActivity.nativeHudMelee();
                } else if (in(x, y, swapCx, swapCy, swapR * 1.15f) && swapPtr < 0) {
                    swapPtr = id;
                    GameActivity.nativeHudSwap();
                } else if (in(x, y, zoomCx, zoomCy, zoomR * 1.15f) && zoomPtr < 0) {
                    zoomPtr = id;
                    GameActivity.nativeHudZoom();
                } else if (in(x, y, dbgCx, dbgCy, dbgR * 1.25f) && dbgPtr < 0) {
                    dbgPtr = id;
                    GameActivity.nativeHudDebug(0);
                } else if (in(x, y, nadeCx, nadeCy, nadeR * 1.15f) && nadePtr < 0) {
                    nadePtr = id;
                    GameActivity.nativeHudGrenade();
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
                    nadePtr = -1;
                    dbgPtr = -1;
                } else {
                    if (id == stickPtr) releaseStick();
                    if (id == firePtr) releaseFire();
                    if (id == jumpPtr) releaseJump();
                    if (id == crouchPtr) releaseCrouch();
                    if (id == reloadPtr) reloadPtr = -1;
                    if (id == meleePtr) meleePtr = -1;
                    if (id == swapPtr) swapPtr = -1;
                    if (id == zoomPtr) zoomPtr = -1;
                    if (id == nadePtr) nadePtr = -1;
                    if (id == dbgPtr) dbgPtr = -1;
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

        @Override
        protected void onDraw(Canvas c) {
            int vehicleMode = GameActivity.nativeVehicleMode();
            if (vehicleMode != 0) {
                float savedSize = label.getTextSize();
                label.setTextSize(Math.min(getWidth(), getHeight()) * 0.026f);
                c.drawText(vehicleMode == 2
                        ? "Stick: drive / steer    BRAKE to stop    EXIT when stopped"
                        : "DRIVE: enter Warthog", getWidth() * 0.5f, getHeight() * 0.18f, label);
                label.setTextSize(savedSize);
            }
            c.drawCircle(stickCx, stickCy, stickR, fill);
            c.drawCircle(stickCx, stickCy, stickR, ring);
            c.drawCircle(stickTx, stickTy, stickR * 0.38f, thumb);

            c.drawCircle(dbgCx, dbgCy, dbgR, dbgP);
            c.drawCircle(dbgCx, dbgCy, dbgR, ring);
            c.drawText("DBG", dbgCx, dbgCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(fireCx, fireCy, fireR, fireP);
            c.drawCircle(fireCx, fireCy, fireR, ring);
            c.drawText("FIRE", fireCx, fireCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(jumpCx, jumpCy, jumpR, jumpP);
            c.drawCircle(jumpCx, jumpCy, jumpR, ring);
            c.drawText(vehicleMode == 2 ? "BRAKE" : "JUMP", jumpCx, jumpCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(crouchCx, crouchCy, crouchR, crouchP);
            c.drawCircle(crouchCx, crouchCy, crouchR, ring);
            c.drawText("CROUCH", crouchCx, crouchCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(reloadCx, reloadCy, reloadR, reloadP);
            c.drawCircle(reloadCx, reloadCy, reloadR, ring);
            c.drawText("RELOAD", reloadCx, reloadCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(meleeCx, meleeCy, meleeR, meleeP);
            c.drawCircle(meleeCx, meleeCy, meleeR, ring);
            c.drawText("MELEE", meleeCx, meleeCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(swapCx, swapCy, swapR, swapP);
            c.drawCircle(swapCx, swapCy, swapR, ring);
            c.drawText(vehicleMode == 2 ? "EXIT" :
                    vehicleMode == 1 ? "DRIVE" : "SWAP", swapCx, swapCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(zoomCx, zoomCy, zoomR, zoomP);
            c.drawCircle(zoomCx, zoomCy, zoomR, ring);
            c.drawText("ZOOM", zoomCx, zoomCy + label.getTextSize() * 0.35f, label);
            c.drawCircle(nadeCx, nadeCy, nadeR, zoomP);
            c.drawCircle(nadeCx, nadeCy, nadeR, ring);
            c.drawText("NADE", nadeCx, nadeCy + label.getTextSize() * 0.35f, label);

            /* Ammo, big and bottom-right: loaded / reserve, "--" while the
             * magazine is out. */
            String a = null;
            try { a = nativeAmmoText(); } catch (Throwable ignored) { }
            if (a != null && a.length() > 0)
                c.drawText(a, getWidth() - 28f, getHeight() * 0.30f, ammo);

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
            postInvalidateDelayed(200);
        }
    }
}
