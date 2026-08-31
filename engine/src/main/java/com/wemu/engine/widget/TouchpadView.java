package com.wemu.engine.widget;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.Log;
import android.os.Handler;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.preference.PreferenceManager;

import com.wemu.engine.R;
import com.wemu.engine.core.AppUtils;
import com.wemu.engine.math.Mathf;
import com.wemu.engine.math.XForm;
import com.wemu.engine.renderer.ViewTransformation;
import com.wemu.engine.winhandler.MouseEventFlags;
import com.wemu.engine.winhandler.WinHandler;
import com.wemu.engine.xserver.Pointer;
import com.wemu.engine.xserver.XServer;

public class TouchpadView extends View {
    private static final byte MAX_FINGERS = 4;
    private static final short MAX_TWO_FINGERS_SCROLL_DISTANCE = 350;
    public static final byte MAX_TAP_TRAVEL_DISTANCE = 10;
    public static final short MAX_TAP_MILLISECONDS = 200;
    public static final float CURSOR_ACCELERATION = 1.25f;
    public static final byte CURSOR_ACCELERATION_THRESHOLD = 6;
    private final Finger[] fingers = new Finger[MAX_FINGERS];
    private byte numFingers = 0;
    private float sensitivity = 1.0f;
    private boolean pointerButtonLeftEnabled = true;
    private boolean pointerButtonRightEnabled = true;
    private Finger fingerPointerButtonLeft;
    private Finger fingerPointerButtonRight;
    private float scrollAccumY = 0;
    private boolean scrolling = false;
    private final XServer xServer;
    private Runnable fourFingersTapCallback;
    private final float[] xform = XForm.getInstance();
    private boolean simTouchScreen = false;
    private boolean continueClick = true;
    private int lastTouchedPosX;
    private int lastTouchedPosY;
    private static final Byte CLICK_DELAYED_TIME = 50;
    private static final Byte EFFECTIVE_TOUCH_DISTANCE = 20;
    private float resolutionScale;
    private static final int UPDATE_FORM_DELAYED_TIME = 50;
    private boolean mouseEnabled = true;
    private boolean externalMousePositionValid = false;
    private float lastExternalMouseX;
    private float lastExternalMouseY;
    /** Buttons already forwarded to the guest, used to de-duplicate DOWN/BUTTON_PRESS pairs. */
    private int externalMouseButtons;
    /** Next time a high-frequency external-mouse movement may be logged. */
    private long nextExternalMouseTraceAt;

    private Handler timeoutHandler; // Reference to the activity's timeout handler
    private Runnable hideControlsRunnable; // Runnable to hide the controls

    private SharedPreferences preferences;


    // Flag to control touchpad vs touchscreen mode

    @SuppressLint("ResourceType")
    public TouchpadView(Context context, XServer xServer, Handler timeoutHandler, Runnable hideControlsRunnable) {
        super(context);
        this.xServer = xServer;

        this.timeoutHandler = timeoutHandler; // Store the reference to timeout handler
        this.hideControlsRunnable = hideControlsRunnable; // Store the reference to the hide controls runnable

        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setBackground(createTransparentBg());
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(false);
        setPointerIcon(PointerIcon.load(getResources(), R.drawable.hidden_pointer_arrow));
        updateXform(AppUtils.getScreenWidth(), AppUtils.getScreenHeight(), guestWidth(), guestHeight());
        // Initialize SharedPreferences here
        this.preferences = PreferenceManager.getDefaultSharedPreferences(context);

        this.timeoutHandler = timeoutHandler; // Store the reference to timeout handler
        this.hideControlsRunnable = hideControlsRunnable; // Store the reference to the hide controls runnable

        // Set up the generic motion listener for hover events
        setOnGenericMotionListener(new OnGenericMotionListener() {
            @Override
            public boolean onGenericMotion(View v, MotionEvent event) {
                if (event.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS) {
                    return handleStylusHoverEvent(event);
                }
                return false;
            }
        });
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateXform(w, h, guestWidth(), guestHeight());
        resolutionScale = 1000.0f / Math.min(guestWidth(), guestHeight());
    }

    /**
     * The guest rectangle this view covers, which is the primary monitor.
     *
     * This view sits over the top panel, so a touch on it has to land in the
     * top panel's part of the root window. Scaling against a root that spans
     * both screens would put a tap at the bottom of this view halfway down the
     * guest desktop.
     */
    private int guestWidth() { return xServer.getPrimaryMonitor().width; }

    private int guestHeight() { return xServer.getPrimaryMonitor().height; }

    private void updateXform(int outerWidth, int outerHeight, int innerWidth, int innerHeight) {
        ViewTransformation viewTransformation = new ViewTransformation();
        viewTransformation.update(outerWidth, outerHeight, innerWidth, innerHeight);

        float invAspect = 1.0f / viewTransformation.aspect;
        if (!xServer.getRenderer().isFullscreen()) {
            XForm.makeTranslation(xform, -viewTransformation.viewOffsetX, -viewTransformation.viewOffsetY);
            XForm.scale(xform, invAspect, invAspect);
        } else
            XForm.makeScale(xform, (float) innerWidth / outerWidth, (float) innerHeight / outerHeight);
    }

    private class Finger {
        private int x;
        private int y;
        private final int startX;
        private final int startY;
        private int lastX;
        private int lastY;
        private final long touchTime;

        public Finger(float x, float y) {
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = this.startX = this.lastX = (int)transformedPoint[0];
            this.y = this.startY = this.lastY = (int)transformedPoint[1];
            touchTime = System.currentTimeMillis();
        }

        public void update(float x, float y) {
            lastX = this.x;
            lastY = this.y;
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = (int)transformedPoint[0];
            this.y = (int)transformedPoint[1];
        }

        private int deltaX() {
            float dx = (x - lastX) * sensitivity;
            if (Math.abs(dx) > CURSOR_ACCELERATION_THRESHOLD) dx *= CURSOR_ACCELERATION;
            return Mathf.roundPoint(dx);
        }

        private int deltaY() {
            float dy = (y - lastY) * sensitivity;
            if (Math.abs(dy) > CURSOR_ACCELERATION_THRESHOLD) dy *= CURSOR_ACCELERATION;
            return Mathf.roundPoint(dy);
        }

        private boolean isTap() {
            return (System.currentTimeMillis() - touchTime) < MAX_TAP_MILLISECONDS && travelDistance() < MAX_TAP_TRAVEL_DISTANCE;
        }

        private float travelDistance() {
            return (float)Math.hypot(x - startX, y - startY);
        }
    }

//    public void setTouchscreenMode(boolean isTouchscreenMode) {
//        this.isTouchscreenMode = isTouchscreenMode;
//    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // If mouse is disabled, ignore all input
        if (!mouseEnabled) return true;

        /*
         * A physical Android mouse does not use one consistent dispatch path.
         * Hover and wheel packets normally arrive through generic motion, but
         * several firmwares (including the Thor's ODIN virtual mouse) deliver
         * button down/up and some moves through the touch path.  The old
         * touchpad handler intentionally ignored mouse DOWN, so a perfectly
         * live Windows cursor could appear frozen as soon as an application
         * needed a click.  Forward mouse packets before interpreting the event
         * as a finger gesture.  Activity-level routing normally gets here
         * first; this is the safe fallback for every other TouchpadView host.
         */
        if (onExternalMouseEvent(event)) return true;
        
        boolean isTouchscreenMode = preferences.getBoolean("touchscreen_toggle", false);

        // Reset the timeout timer to keep controls visible
        resetTouchscreenTimeout();  // <-- Ensure the controls stay visible

        // Continue handling touch events as usual
        int toolType = event.getToolType(0);

        if (toolType == MotionEvent.TOOL_TYPE_STYLUS) {
            return handleStylusEvent(event);
        } else if (isTouchscreenMode) {
            return handleTouchscreenEvent(event);
        } else {
            return handleTouchpadEvent(event);
        }
    }

    private void resetTouchscreenTimeout() {
        //Log.d("TouchpadView", "Touch detected, resetting timeout.");
        if (timeoutHandler != null && hideControlsRunnable != null) {
            // Cancel any pending hide requests
            timeoutHandler.removeCallbacks(hideControlsRunnable);
            // Post a new request to hide the controls after 5 seconds
            timeoutHandler.postDelayed(hideControlsRunnable, 5000); // Adjust timeout as necessary
        }
    }
    private boolean handleStylusHoverEvent(MotionEvent event) {
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_HOVER_ENTER:
                Log.d("StylusEvent", "Hover Enter");
                break;
            case MotionEvent.ACTION_HOVER_MOVE:
                Log.d("StylusEvent", "Hover Move: (" + event.getX() + ", " + event.getY() + ")");
                float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
                break;
            case MotionEvent.ACTION_HOVER_EXIT:
                Log.d("StylusEvent", "Hover Exit");
                break;
            default:
                return false;
        }
        return true;
    }

    private boolean handleStylusEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int buttonState = event.getButtonState();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                if ((buttonState & MotionEvent.BUTTON_SECONDARY) != 0) {
                    handleStylusRightClick(event);
                } else {
                    handleStylusLeftClick(event);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                handleStylusMove(event);
                break;
            case MotionEvent.ACTION_UP:
                handleStylusUp(event);
                break;
        }

        return true;
    }

    private void handleStylusLeftClick(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
    }

    private void handleStylusRightClick(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
    }

    private void handleStylusMove(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
    }

    private void handleStylusUp(MotionEvent event) {
        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
    }



    private boolean handleTouchpadEvent(MotionEvent event) {
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        int actionMasked = event.getActionMasked();
        if (pointerId >= MAX_FINGERS) return true;

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return true;
                scrollAccumY = 0;
                scrolling = false;
                fingers[pointerId] = new Finger(event.getX(actionIndex), event.getY(actionIndex));
                numFingers++;
                if (simTouchScreen) {
                    final Runnable clickDelay = () -> {
                        if (continueClick) {
                            xServer.injectPointerMove(lastTouchedPosX, lastTouchedPosY);
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                        }
                    };
                    if (pointerId == 0) {
                        continueClick = true;
                        if (Math.hypot(fingers[0].x - lastTouchedPosX, fingers[0].y - lastTouchedPosY) * resolutionScale > EFFECTIVE_TOUCH_DISTANCE) {
                            lastTouchedPosX = fingers[0].x;
                            lastTouchedPosY = fingers[0].y;
                        }
                        postDelayed(clickDelay, CLICK_DELAYED_TIME);
                    } else if (pointerId == 1) {
                        // When put a finger on InputControl, such as a button.
                        // The pointerId that TouchPadView got won't increase from 1, so map 1 as 0 here.
                        if (numFingers < 2) {
                            continueClick = true;
                            if (Math.hypot(fingers[1].x - lastTouchedPosX, fingers[1].y - lastTouchedPosY) * resolutionScale > EFFECTIVE_TOUCH_DISTANCE) {
                                lastTouchedPosX = fingers[1].x;
                                lastTouchedPosY = fingers[1].y;
                            }
                            postDelayed(clickDelay, CLICK_DELAYED_TIME);
                        } else
                            continueClick = System.currentTimeMillis() - fingers[0].touchTime > CLICK_DELAYED_TIME;
                    }
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
                    handleExternalMouseMove(event);
                } else {
                    for (byte i = 0; i < MAX_FINGERS; i++) {
                        if (fingers[i] != null) {
                            int pointerIndex = event.findPointerIndex(i);
                            if (pointerIndex >= 0) {
                                fingers[i].update(event.getX(pointerIndex), event.getY(pointerIndex));
                                handleFingerMove(fingers[i]);
                            } else {
                                handleFingerUp(fingers[i]);
                                fingers[i] = null;
                                numFingers--;
                            }
                        }
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (fingers[pointerId] != null) {
                    fingers[pointerId].update(event.getX(actionIndex), event.getY(actionIndex));
                    handleFingerUp(fingers[pointerId]);
                    fingers[pointerId] = null;
                    numFingers--;
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                for (byte i = 0; i < MAX_FINGERS; i++) fingers[i] = null;
                numFingers = 0;
                break;
        }

        return true;
    }

    private boolean handleTouchscreenEvent(MotionEvent event) {
        int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handleTouchDown(event);
                break;
            case MotionEvent.ACTION_MOVE:
                if (event.getPointerCount() == 2) {
                    handleTwoFingerScroll(event);
                } else {
                    handleTouchMove(event);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (event.getPointerCount() == 2) {
                    handleTwoFingerTap(event);
                } else {
                    handleTouchUp(event);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                // A cancelled gesture is not a click of any kind, so the pending
                // long press goes with it rather than firing at a finger that
                // is no longer there.
                removeCallbacks(longPressAction);
                longPressFired = false;
                leftButtonDown = false;
                if (xServer.isRelativeMouseMovement()) {
                    xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTUP, 0, 0, 0);
                    xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
                }
                else {
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                }
                break;
        }
        return true;
    }

    /*
     * Touchscreen mode: a tap is a left click, a hold is a right click.
     *
     * The left button used to go down on `ACTION_DOWN` and up on `ACTION_UP`,
     * which is the obvious mapping and the reason a long press could only ever
     * be a long left click. There was a comment here promising the rest.
     *
     * Deciding on release rather than on touch is what makes both gestures
     * possible from one finger: until the finger has either moved, lifted, or
     * held past the threshold, nobody knows which of the three it is. So the
     * press is withheld and one of three things resolves it —
     *
     *   moved  → a drag, so put the left button down and let it drag
     *   lifted → a tap, so a left press and release at that spot
     *   held   → a right click, and the left button never happens at all
     *
     * The cost is that a left click lands on release instead of on touch. That
     * is the standard bargain on every touch platform, and this is the mode for
     * driving an interface rather than for aiming in a shooter — a game wanting
     * the immediate press wants relative mouse movement, which is captured and
     * takes a different path entirely.
     */
    private static final int LONG_PRESS_TIME = 450;
    private static final int LONG_PRESS_SLOP = 24;

    private float touchDownX, touchDownY;
    private boolean longPressFired;
    private boolean leftButtonDown;
    // A method reference rather than a lambda body: `xServer` is assigned in the
    // constructor, and a field initialiser that reads it does not compile.
    private final Runnable longPressAction = this::fireLongPress;

    private void fireLongPress() {
        // Reached only if the finger neither moved nor lifted, so there is no
        // left button to undo — see the note above.
        longPressFired = true;
        if (xServer.isRelativeMouseMovement()) {
            xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTDOWN, 0, 0, 0);
            xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
        } else {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
        }
    }

    private void movePointerTo(MotionEvent event) {
        float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
        if (xServer.isRelativeMouseMovement())
            xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, (int)transformedPoint[0], (int)transformedPoint[1], 0);
        else
            xServer.injectPointerMove((int) transformedPoint[0], (int) transformedPoint[1]);
    }

    private void pressLeft() {
        if (leftButtonDown) return;
        leftButtonDown = true;
        if (xServer.isRelativeMouseMovement())
            xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTDOWN, 0, 0, 0);
        else
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
    }

    private void releaseLeft() {
        if (!leftButtonDown) return;
        leftButtonDown = false;
        if (xServer.isRelativeMouseMovement())
            xServer.getWinHandler().mouseEvent(MouseEventFlags.LEFTUP, 0, 0, 0);
        else
            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
    }

    private void handleTouchDown(MotionEvent event) {
        movePointerTo(event);

        if (event.getPointerCount() == 1) {
            touchDownX = event.getX();
            touchDownY = event.getY();
            longPressFired = false;
            removeCallbacks(longPressAction);
            postDelayed(longPressAction, LONG_PRESS_TIME);
        } else {
            // A second finger means this was never a long press; it is a scroll
            // or a two-finger tap, both of which own the gesture from here.
            removeCallbacks(longPressAction);
        }
    }

    private void handleTouchMove(MotionEvent event) {
        movePointerTo(event);

        if (longPressFired) return;

        // Past the slop it is a drag, so the press it was withholding is owed.
        if (Math.hypot(event.getX() - touchDownX, event.getY() - touchDownY) > LONG_PRESS_SLOP) {
            removeCallbacks(longPressAction);
            pressLeft();
        }
    }

    private void handleTouchUp(MotionEvent event) {
        removeCallbacks(longPressAction);

        if (longPressFired) {
            longPressFired = false;
            return;
        }

        if (leftButtonDown) {
            releaseLeft();
        } else {
            // Lifted without moving and without holding: a tap, which is a
            // press and a release in one go at the point last moved to.
            pressLeft();
            releaseLeft();
        }
    }

    private void handleTwoFingerScroll(MotionEvent event) {
        float scrollDistance = event.getY(0) - event.getY(1);
        if (Math.abs(scrollDistance) > 10) {
            if (scrollDistance > 0) {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
            } else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
            }
        }
    }

    private void handleTwoFingerTap(MotionEvent event) {
        if (event.getPointerCount() == 2) {
            if (xServer.isRelativeMouseMovement()) {
                xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTDOWN, 0, 0, 0);
                xServer.getWinHandler().mouseEvent(MouseEventFlags.RIGHTUP, 0, 0, 0);
            }
            else {
                xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
            }
        }
    }



    private void handleFingerUp(Finger finger1) {
        switch (numFingers) {
            case 1:
                if (simTouchScreen) {
                    final Runnable clickDelay = () -> {
                        if (continueClick)
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    };
                    postDelayed(clickDelay, CLICK_DELAYED_TIME);
                }
                else if (finger1.isTap()) pressPointerButtonLeft(finger1);
                break;
            case 2:
                Finger finger2 = findSecondFinger(finger1);
                if (finger2 != null && finger1.isTap()) pressPointerButtonRight(finger1);
                break;
            case 4:
                if (fourFingersTapCallback != null) {
                    for (byte i = 0; i < 4; i++) {
                        if (fingers[i] != null && !fingers[i].isTap()) return;
                    }
                    fourFingersTapCallback.run();
                }
                break;
        }

        releasePointerButtonLeft(finger1);
        releasePointerButtonRight(finger1);
    }

    private void handleFingerMove(Finger finger1) {
        boolean skipPointerMove = false;

        Finger finger2 = numFingers == 2 ? findSecondFinger(finger1) : null;
        if (finger2 != null) {
            final float resolutionScale = 1000.0f / Math.min(guestWidth(), guestHeight());
            float currDistance = (float)Math.hypot(finger1.x - finger2.x, finger1.y - finger2.y) * resolutionScale;

            if (currDistance < MAX_TWO_FINGERS_SCROLL_DISTANCE) {
                scrollAccumY += ((finger1.y + finger2.y) * 0.5f) - (finger1.lastY + finger2.lastY) * 0.5f;

                if (scrollAccumY < -100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                    scrollAccumY = 0;
                }
                else if (scrollAccumY > 100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                    scrollAccumY = 0;
                }
                scrolling = true;
            }
            else if (currDistance >= MAX_TWO_FINGERS_SCROLL_DISTANCE && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT) &&
                     finger2.travelDistance() < MAX_TAP_TRAVEL_DISTANCE) {
                pressPointerButtonLeft(finger1);
                skipPointerMove = true;
            }
        }

        if (!scrolling && numFingers <= 2 && !skipPointerMove) {
            int dx = finger1.deltaX();
            int dy = finger1.deltaY();

            if (simTouchScreen) {
                if (System.currentTimeMillis() - finger1.touchTime > CLICK_DELAYED_TIME)
                    xServer.injectPointerMove(finger1.x, finger1.y);
            }
            else if (xServer.isRelativeMouseMovement()) {
                WinHandler winHandler = xServer.getWinHandler();
                winHandler.mouseEvent(MouseEventFlags.MOVE, dx, dy, 0);
            }
            else xServer.injectPointerMoveDelta(dx, dy);
        }
    }

    private Finger findSecondFinger(Finger finger) {
        for (byte i = 0; i < MAX_FINGERS; i++) {
            if (fingers[i] != null && fingers[i] != finger) return fingers[i];
        }
        return null;
    }

    private void pressPointerButtonLeft(Finger finger) {
        if (pointerButtonLeftEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
            fingerPointerButtonLeft = finger;
        }
    }

    private void pressPointerButtonRight(Finger finger) {
        if (pointerButtonRightEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
            fingerPointerButtonRight = finger;
        }
    }

    private void releasePointerButtonLeft(final Finger finger) {
        if (pointerButtonLeftEnabled && finger == fingerPointerButtonLeft && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                fingerPointerButtonLeft = null;
            }, 30);
        }
    }

    private void releasePointerButtonRight(final Finger finger) {
        if (pointerButtonRightEnabled && finger == fingerPointerButtonRight && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                fingerPointerButtonRight = null;
            }, 30);
        }
    }

    public void setSensitivity(float sensitivity) {
        this.sensitivity = sensitivity;
    }

    private int lastMouseMoveX;
    private int lastMouseMoveY;

    // Permite que um ControlElement comum (um botão) atue como uma pequena área de
    // arrasto para mover o cursor do mouse, sem precisar de um TRACKPAD dedicado.
    // Espelha a mesma lógica de delta/aceleração usada pelos gestos normais do touchpad.
    public void mouseMove(float x, float y, int action) {
        float[] transformedPoint = XForm.transformPoint(xform, x, y);
        int tx = (int)transformedPoint[0];
        int ty = (int)transformedPoint[1];

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                lastMouseMoveX = tx;
                lastMouseMoveY = ty;
                break;
            case MotionEvent.ACTION_MOVE: {
                float dxF = (tx - lastMouseMoveX) * sensitivity;
                if (Math.abs(dxF) > CURSOR_ACCELERATION_THRESHOLD) dxF *= CURSOR_ACCELERATION;
                float dyF = (ty - lastMouseMoveY) * sensitivity;
                if (Math.abs(dyF) > CURSOR_ACCELERATION_THRESHOLD) dyF *= CURSOR_ACCELERATION;
                int dx = Mathf.roundPoint(dxF);
                int dy = Mathf.roundPoint(dyF);
                lastMouseMoveX = tx;
                lastMouseMoveY = ty;

                if (dx != 0 || dy != 0) {
                    if (xServer.isRelativeMouseMovement())
                        xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, dx, dy, 0);
                    else
                        xServer.injectPointerMoveDelta(dx, dy);
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
            default:
                break;
        }
    }

    public boolean isPointerButtonLeftEnabled() {
        return pointerButtonLeftEnabled;
    }

    public void setPointerButtonLeftEnabled(boolean pointerButtonLeftEnabled) {
        this.pointerButtonLeftEnabled = pointerButtonLeftEnabled;
    }

    public boolean isPointerButtonRightEnabled() {
        return pointerButtonRightEnabled;
    }

    public void setPointerButtonRightEnabled(boolean pointerButtonRightEnabled) {
        this.pointerButtonRightEnabled = pointerButtonRightEnabled;
    }

    public void setFourFingersTapCallback(Runnable fourFingersTapCallback) {
        this.fourFingersTapCallback = fourFingersTapCallback;
    }

    public boolean onExternalMouseEvent(MotionEvent event) {
        boolean handled = false;
        if (isExternalMouse(event)) {
            int actionButton = event.getActionButton();
            switch (event.getActionMasked()) {
                /*
                 * Android mice are allowed to report either DOWN/UP or the
                 * more specific BUTTON_PRESS/BUTTON_RELEASE pair.  The Thor's
                 * ODIN virtual mouse uses the former on some firmware builds.
                 * Keeping one forwarded mask also prevents devices which emit
                 * both pairs from clicking twice.
                 */
                case MotionEvent.ACTION_DOWN:
                    syncExternalMouseButtons(event.getButtonState() != 0
                            ? event.getButtonState() : MotionEvent.BUTTON_PRIMARY);
                    handled = true;
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    syncExternalMouseButtons(0);
                    handled = true;
                    break;
                case MotionEvent.ACTION_BUTTON_PRESS:
                    syncExternalMouseButtons(event.getButtonState() | actionButton);
                    handled = true;
                    break;
                case MotionEvent.ACTION_BUTTON_RELEASE:
                    syncExternalMouseButtons(event.getButtonState() & ~actionButton);
                    handled = true;
                    break;
                case MotionEvent.ACTION_MOVE:
                case MotionEvent.ACTION_HOVER_MOVE:
                    handleExternalMouseMove(event);
                    handled = true;
                    break;
                case MotionEvent.ACTION_HOVER_EXIT:
                    externalMousePositionValid = false;
                    handled = true;
                    break;
                case MotionEvent.ACTION_SCROLL:
                    float scrollY = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                    if (scrollY <= -1.0f) {
                        if (xServer.isRelativeMouseMovement())
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.WHEEL, 0, 0, (int)scrollY);
                        else {
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                        }
                    } else if (scrollY >= 1.0f) {
                        if (xServer.isRelativeMouseMovement())
                            xServer.getWinHandler().mouseEvent(MouseEventFlags.WHEEL, 0, 0,(int)scrollY);
                        else {
                            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                            xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                        }
                    }
                    handled = true;
                    break;
            }
            traceExternalMouseEvent(event, handled);
        }
        return handled;
    }

    /**
     * A bounded trace of the Android-to-X mouse boundary.
     *
     * Movement is capped at two lines per second; buttons are always recorded.
     * This is intentionally about raw/mapped state rather than every packet so
     * a launcher input failure can be diagnosed without turning logcat into a
     * frame-time problem.
     */
    private void traceExternalMouseEvent(MotionEvent event, boolean handled) {
        int action = event.getActionMasked();
        long now = event.getEventTime();
        boolean movement = action == MotionEvent.ACTION_MOVE
                || action == MotionEvent.ACTION_HOVER_MOVE;
        if (movement && now < nextExternalMouseTraceAt) return;
        if (movement) nextExternalMouseTraceAt = now + 500L;

        int tool = event.getPointerCount() > 0 ? event.getToolType(0) : -1;
        Log.i("WemuMouseTrace", "action=" + MotionEvent.actionToString(action)
                + " handled=" + handled
                + " source=0x" + Integer.toHexString(event.getSource())
                + " tool=" + tool
                + " xy=" + event.getX() + "," + event.getY()
                + " raw=" + event.getRawX() + "," + event.getRawY()
                + " rel=" + event.getAxisValue(MotionEvent.AXIS_RELATIVE_X)
                + "," + event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y)
                + " rxry=" + event.getAxisValue(MotionEvent.AXIS_RX)
                + "," + event.getAxisValue(MotionEvent.AXIS_RY)
                + " buttons=0x" + Integer.toHexString(event.getButtonState())
                + " xRelative=" + xServer.isRelativeMouseMovement()
                + " view=" + getWidth() + "x" + getHeight()
                + " guest=" + guestWidth() + "x" + guestHeight());
    }

    /** Captured pointers bypass generic-motion dispatch and arrive here. */
    @Override
    public boolean onCapturedPointerEvent(MotionEvent event) {
        return onExternalMouseEvent(event) || super.onCapturedPointerEvent(event);
    }

    private static boolean isExternalMouse(MotionEvent event) {
        if (event == null) return false;
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)
                || event.isFromSource(InputDevice.SOURCE_MOUSE_RELATIVE)) return true;
        return event.getPointerCount() > 0
                && event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE;
    }

    /** Mirrors Android's current mouse-button mask into Wine/X exactly once. */
    private void syncExternalMouseButtons(int newButtons) {
        syncExternalMouseButton(
                MotionEvent.BUTTON_PRIMARY,
                newButtons,
                MouseEventFlags.LEFTDOWN,
                MouseEventFlags.LEFTUP,
                Pointer.Button.BUTTON_LEFT);
        syncExternalMouseButton(
                MotionEvent.BUTTON_SECONDARY,
                newButtons,
                MouseEventFlags.RIGHTDOWN,
                MouseEventFlags.RIGHTUP,
                Pointer.Button.BUTTON_RIGHT);
        syncExternalMouseButton(
                MotionEvent.BUTTON_TERTIARY,
                newButtons,
                MouseEventFlags.MIDDLEDOWN,
                MouseEventFlags.MIDDLEUP,
                Pointer.Button.BUTTON_MIDDLE);
        externalMouseButtons = newButtons;
    }

    private void syncExternalMouseButton(
            int androidButton,
            int newButtons,
            int downFlag,
            int upFlag,
            Pointer.Button xButton) {
        boolean wasDown = (externalMouseButtons & androidButton) != 0;
        boolean isDown = (newButtons & androidButton) != 0;
        if (wasDown == isDown) return;

        if (xServer.isRelativeMouseMovement()) {
            xServer.getWinHandler().mouseEvent(isDown ? downFlag : upFlag, 0, 0, 0);
        } else if (isDown) {
            xServer.injectPointerButtonPress(xButton);
        } else {
            xServer.injectPointerButtonRelease(xButton);
        }
    }

    private void handleExternalMouseMove(MotionEvent event) {
        float[] absolute = XForm.transformPoint(xform, event.getX(), event.getY());

        /*
         * The AYN/ODIN firmware's built-in "ODIN Station Virtual Mouse" is a
         * relative mouse, but its kernel device advertises REL_RX/REL_RY rather
         * than REL_X/REL_Y.  Android consequently exposes its deltas through
         * AXIS_RX/AXIS_RY.  Reading only AXIS_RELATIVE_X/Y made every movement
         * zero and left Skyrim's menu pointer pinned in place.
         */
        float dx = event.getAxisValue(MotionEvent.AXIS_RELATIVE_X);
        float dy = event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y);
        boolean hasRelativeAxes = hasMouseAxis(event, MotionEvent.AXIS_RELATIVE_X)
                || hasMouseAxis(event, MotionEvent.AXIS_RELATIVE_Y);
        if (!hasRelativeAxes) {
            dx = event.getAxisValue(MotionEvent.AXIS_RX);
            dy = event.getAxisValue(MotionEvent.AXIS_RY);
            hasRelativeAxes = hasMouseAxis(event, MotionEvent.AXIS_RX)
                    || hasMouseAxis(event, MotionEvent.AXIS_RY);
        }
        if (!hasRelativeAxes && event.isFromSource(InputDevice.SOURCE_MOUSE_RELATIVE)) {
            // Android's captured-pointer API carries deltas in X/Y.
            dx = event.getX();
            dy = event.getY();
            hasRelativeAxes = true;
        }

        if (hasRelativeAxes) {
            float[] transformedDelta = computeDeltaPoint(0.0f, 0.0f, dx, dy);
            int moveX = Math.round(transformedDelta[0]);
            int moveY = Math.round(transformedDelta[1]);
            if (moveX != 0 || moveY != 0) {
                if (xServer.isRelativeMouseMovement()) {
                    xServer.getWinHandler().mouseEvent(MouseEventFlags.MOVE, moveX, moveY, 0);
                } else {
                    xServer.injectPointerMoveDelta(moveX, moveY);
                }
            }
            externalMousePositionValid = false;
            return;
        }

        if (!xServer.isRelativeMouseMovement()) {
            externalMousePositionValid = true;
            lastExternalMouseX = absolute[0];
            lastExternalMouseY = absolute[1];
            xServer.injectPointerMove((int)absolute[0], (int)absolute[1]);
            return;
        }

        if (externalMousePositionValid) {
            dx = absolute[0] - lastExternalMouseX;
            dy = absolute[1] - lastExternalMouseY;
        } else {
            // The first absolute event establishes an origin; treating it as a
            // delta is what made a captured FPS camera snap across the scene.
            dx = 0.0f;
            dy = 0.0f;
        }
        externalMousePositionValid = true;
        lastExternalMouseX = absolute[0];
        lastExternalMouseY = absolute[1];

        if (dx != 0.0f || dy != 0.0f) {
            xServer.getWinHandler().mouseEvent(
                    MouseEventFlags.MOVE, Math.round(dx), Math.round(dy), 0);
        }
    }

    private static boolean hasMouseAxis(MotionEvent event, int axis) {
        InputDevice device = event.getDevice();
        return device != null && (device.getMotionRange(axis, event.getSource()) != null
                || device.getMotionRange(axis) != null);
    }


    public float[] computeDeltaPoint(float lastX, float lastY, float x, float y) {
        final float[] result = {0, 0};

        XForm.transformPoint(xform, lastX, lastY, result);
        lastX = result[0];
        lastY = result[1];

        XForm.transformPoint(xform, x, y, result);
        x = result[0];
        y = result[1];

        result[0] = x - lastX;
        result[1] = y - lastY;
        return result;
    }

    private StateListDrawable createTransparentBg() {
        StateListDrawable stateListDrawable = new StateListDrawable();

        ColorDrawable focusedDrawable = new ColorDrawable(Color.TRANSPARENT);
        ColorDrawable defaultDrawable = new ColorDrawable(Color.TRANSPARENT);

        stateListDrawable.addState(new int[]{android.R.attr.state_focused}, focusedDrawable);
        stateListDrawable.addState(new int[]{}, defaultDrawable);

        return stateListDrawable;
    }

    public void setSimTouchScreen(boolean simTouchScreen) {
        this.simTouchScreen = simTouchScreen;
        xServer.setSimulateTouchScreen(this.simTouchScreen);
    }

    public boolean isSimTouchScreen() {
        return simTouchScreen;
    }

    public void toggleFullscreen() {
        new Handler().postDelayed(() -> updateXform(getWidth(), getHeight(), guestWidth(), guestHeight()),
                UPDATE_FORM_DELAYED_TIME);
    }
    
    public void setMouseEnabled(boolean enabled) {
        this.mouseEnabled = enabled;
    }
}
