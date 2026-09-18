package net.hta.halotrial;

import android.app.NativeActivity;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

/**
 * NativeActivity plus a COD-Mobile-style touch HUD: visible stick, a large
 * fire button, and jump. Look is anywhere that isn't a button.
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
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().post(this::attachHud);
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
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_INSET_DECOR,
                PixelFormat.TRANSLUCENT);
        lp.token = getWindow().getDecorView().getWindowToken();
        lp.setTitle("hta-hud");
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

    private static final class HudOverlay extends View {
        private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint thumb = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fireP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint jumpP = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

        private float stickCx, stickCy, stickR, stickTx, stickTy;
        private float fireCx, fireCy, fireR;
        private float jumpCx, jumpCy, jumpR;
        private int stickPtr = -1, firePtr = -1, jumpPtr = -1;
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
            label.setColor(0xFFFFFFFF);
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
            label.setTextSize(m * 0.035f);
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
                } else {
                    if (id == stickPtr) releaseStick();
                    if (id == firePtr) releaseFire();
                    if (id == jumpPtr) releaseJump();
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

        @Override
        protected void onDraw(Canvas c) {
            c.drawCircle(stickCx, stickCy, stickR, fill);
            c.drawCircle(stickCx, stickCy, stickR, ring);
            c.drawCircle(stickTx, stickTy, stickR * 0.38f, thumb);

            c.drawCircle(fireCx, fireCy, fireR, fireP);
            c.drawCircle(fireCx, fireCy, fireR, ring);
            c.drawText("FIRE", fireCx, fireCy + label.getTextSize() * 0.35f, label);

            c.drawCircle(jumpCx, jumpCy, jumpR, jumpP);
            c.drawCircle(jumpCx, jumpCy, jumpR, ring);
            c.drawText("JUMP", jumpCx, jumpCy + label.getTextSize() * 0.35f, label);
        }
    }
}
