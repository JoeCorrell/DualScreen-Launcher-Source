package com.wemu.engine.xserver;

import android.util.Log;
import android.util.SparseArray;

import com.wemu.engine.xserver.events.Event;
import com.wemu.engine.xserver.events.PropertyNotify;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Stack;

public class Window extends XResource {
    /*
     * WM_CLASS is read for every window on every presented frame, so the
     * renderers can tell a game's window from a compatibility shell's. Two
     * costs were being paid each time: finding the atom, and decoding the
     * property into a fresh String. The first is now a constant — atom ids are
     * fixed once Atom's predefined table is built, which is before this field
     * can be read. The second is cached, and dropped whenever the property
     * itself changes.
     */
    private static final int WM_CLASS_ATOM = Atom.getId("WM_CLASS");
    private static final int WM_NAME_ATOM = Atom.getId("WM_NAME");
    private static final int NET_WM_NAME_ATOM = Atom.getId("_NET_WM_NAME");

    public static final int FLAG_X = 1;
    public static final int FLAG_Y = 1<<1;
    public static final int FLAG_WIDTH = 1<<2;
    public static final int FLAG_HEIGHT = 1<<3;
    public static final int FLAG_BORDER_WIDTH = 1<<4;
    public static final int FLAG_SIBLING = 1<<5;
    public static final int FLAG_STACK_MODE = 1<<6;
    public enum StackMode {ABOVE, BELOW, TOP_IF, BOTTOM_IF, OPPOSITE}
    public enum MapState {UNMAPPED, UNVIEWABLE, VIEWABLE}
    public enum WMHints {FLAGS, INPUT, INITIAL_STATE, ICON_PIXMAP, ICON_WINDOW, ICON_X, ICON_Y, ICON_MASK, WINDOW_GROUP}
    private Drawable content;
    private short x;
    private short y;
    private short width;
    private short height;
    private short borderWidth;
    private Window parent;
    public final XClient originClient;
    public final WindowAttributes attributes = new WindowAttributes(this);
    private final SparseArray<Property> properties = new SparseArray<>();
    /** Read from the render thread, written from the X client thread. */
    private volatile String cachedClassName;
    private final ArrayList<Window> children = new ArrayList<>();
    private final List<Window> immutableChildren = Collections.unmodifiableList(children);
    private final ArrayList<EventListener> eventListeners = new ArrayList<>();

    public Window(int id, Drawable content, int x, int y, int width, int height, XClient originClient) {
        super(id);
        this.content = content;
        this.x = (short)x;
        this.y = (short)y;
        this.width = (short)width;
        this.height = (short)height;
        this.originClient = originClient;
    }

    public short getX() {
        return x;
    }

    public void setX(short x) {
        this.x = x;
    }

    public short getY() {
        return y;
    }

    public void setY(short y) {
        this.y = y;
    }

    public short getWidth() {
        return width;
    }

    public void setWidth(short width) {
        this.width = width;
    }

    public short getHeight() {
        return height;
    }

    public void setHeight(short height) {
        this.height = height;
    }

    public short getBorderWidth() {
        return borderWidth;
    }

    public void setBorderWidth(short borderWidth) {
        this.borderWidth = borderWidth;
    }

    public Drawable getContent() {
        return content;
    }

    public void setContent(Drawable content) {
        this.content = content;
    }

    public Window getParent() {
        return parent;
    }

    public void setParent(Window parent) {
        this.parent = parent;
    }

    public Property getProperty(int id) {
        return properties.get(id);
    }

    public void addProperty(Property property) {
        properties.put(property.name, property);
        if (property.name == WM_CLASS_ATOM) cachedClassName = null;
    }

    public void removeProperty(int id) {
        properties.remove(id);
        if (id == WM_CLASS_ATOM) cachedClassName = null;
        sendEvent(Event.PROPERTY_CHANGE, new PropertyNotify(this, id, true));
    }

    public Property modifyProperty(int atom, int type, Property.Format format, Property.Mode mode, byte[] data) {
        Property property = getProperty(atom);
        boolean modified = false;
        if (property == null) {
            addProperty((property = new Property(atom, type, format, data)));
            modified = true;
        }
        else if (mode == Property.Mode.REPLACE) {
            if (property.format == format) {
                property.replace(data);
            }
            else properties.put(atom, new Property(atom, type, format, data));
            modified = true;
        }
        else if (property.format == format && property.type == type) {
            if (mode == Property.Mode.PREPEND) {
                property.prepend(data);
            }
            else if (mode == Property.Mode.APPEND) {
                property.append(data);
            }
            modified = true;
        }

        if (modified) {
            /*
             * Catches the replace-with-a-new-Property path above, which writes
             * to the map directly rather than through addProperty.
             */
            if (atom == WM_CLASS_ATOM) cachedClassName = null;
            sendEvent(Event.PROPERTY_CHANGE, new PropertyNotify(this, atom, false));
            return property;
        }
        else return null;
    }

    public String getName() {
        /*
         * Wine publishes Unicode Win32 titles through the EWMH property on
         * some window paths while leaving the legacy ICCCM title absent (or
         * temporarily empty). Native dual-screen panels rely on their exact
         * private title when Wine clamps a newly-created window onto the
         * primary monitor, so reading WM_NAME alone makes those panels
         * indistinguishable from an anonymous game child window.
         */
        Property modern = getProperty(NET_WM_NAME_ATOM);
        String name = modern != null ? modern.toString() : "";
        if (!name.isEmpty()) return name;

        Property legacy = getProperty(WM_NAME_ATOM);
        return legacy != null ? legacy.toString() : "";
    }

    public String getClassName() {
        String cached = cachedClassName;
        if (cached != null) return cached;
        Property property = getProperty(WM_CLASS_ATOM);
        cached = property != null ? property.toString() : "";
        cachedClassName = cached;
        return cached;
    }

    public int getWMHintsValue(WMHints wmHints) {
        Property property = getProperty(Atom.getId("WM_HINTS"));
        return property != null ? property.getInt(wmHints.ordinal()) : 0;
    }

    public int getProcessId() {
        Property property = getProperty(Atom.getId("_NET_WM_PID"));
        return property != null ? property.getInt(0) : 0;
    }

    public boolean isWoW64() {
        Property property = getProperty(Atom.getId("_NET_WM_WOW64"));
        return property != null && property.data.get(0) == 1;
    }

    public long getHandle() {
        Property property = getProperty(Atom.getId("_NET_WM_HWND"));
        return property != null ? property.getLong(0) : 0;
    }

    public boolean isApplicationWindow() {
        int windowGroup = getWMHintsValue(WMHints.WINDOW_GROUP);
        return attributes.isMapped() && windowGroup == id && width > 1 && height > 1;
    }

    public boolean isInputOutput() {
        return content != null;
    }

    public void addChild(Window child) {
        if (child == null || child.parent == this) return;
        child.parent = this;
        children.add(child);
    }

    public void removeChild(Window child) {
        if (child == null || child.parent != this) return;
        child.parent = null;
        children.remove(child);
    }

    public Window previousSibling() {
        if (parent == null) return null;
        int index = parent.children.indexOf(this);
        return index > 0 ? parent.children.get(index - 1) : null;
    }

    public void moveChildAbove(Window child, Window sibling) {
        children.remove(child);
        if (sibling != null && children.contains(sibling)) {
            children.add(children.indexOf(sibling) + 1, child);
            return;
        }
        children.add(child);
    }

    public void moveChildBelow(Window child, Window sibling) {
        children.remove(child);
        if (sibling != null && children.contains(sibling)) {
            children.add(children.indexOf(sibling), child);
            return;
        }
        children.add(0, child);
    }

    public List<Window> getChildren() {
        return immutableChildren;
    }

    public int getChildCount() {
        return children.size();
    }

    public void addEventListener(EventListener eventListener) {
        eventListeners.add(eventListener);
    }

    public void removeEventListener(EventListener eventListener) {
        eventListeners.remove(eventListener);
    }

    public boolean hasEventListenerFor(int eventId) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(eventId)) return true;
        }
        return false;
    }

    public boolean hasEventListenerFor(Bitmask mask) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(mask)) return true;
        }
        return false;
    }

    public void sendEvent(int eventId, Event event) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(eventId)) {
                eventListener.sendEvent(event);
            }
        }
    }

    public void sendEvent(Bitmask eventMask, Event event) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(eventMask)) {
                eventListener.sendEvent(event);
            }
        }
    }

    public void sendEvent(int eventId, Event event, XClient client) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(eventId) && eventListener.client == client) {
                eventListener.sendEvent(event);
            }
        }
    }

    public void sendEvent(Bitmask eventMask, Event event, XClient client) {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(eventMask) && eventListener.client == client) {
                eventListener.sendEvent(event);
            }
        }
    }

    public void sendEvent(Event event) {
        for (EventListener eventListener : eventListeners) eventListener.sendEvent(event);
    }

    public boolean containsPoint(short rootX, short rootY) {
        short[] localPoint = rootPointToLocal(rootX, rootY);
        return localPoint[0] >= 0 && localPoint[1] >= 0 && localPoint[0] < width && localPoint[1] < height;
    }

    public short[] rootPointToLocal(short x, short y) {
        Window window = this;
        while (window != null) {
            x -= window.x;
            y -= window.y;
            window = window.parent;
        }
        return new short[]{x, y};
    }

    public short[] localPointToRoot(short x, short y) {
        Window window = this;
        while (window != null) {
            x += window.x;
            y += window.y;
            window = window.parent;
        }
        return new short[]{x, y};
    }

    public short getRootX() {
        short rootX = x;
        Window window = parent;
        while (window != null) {
            rootX += window.x;
            window = window.parent;
        }
        return rootX;
    }

    public short getRootY() {
        short rootY = y;
        Window window = parent;
        while (window != null) {
            rootY += window.y;
            window = window.parent;
        }
        return rootY;
    }

    public Window getAncestorWithEventMask(Bitmask eventMask) {
        Window window = this;
        while (window != null) {
            if (window.hasEventListenerFor(eventMask)) return window;
            if (window.attributes.getDoNotPropagateMask().intersects(eventMask)) return null;
            window = window.parent;
        }
        return null;
    }

    public Window getAncestorWithEventId(int eventId) {
        return getAncestorWithEventId(eventId, null);
    }

    public Window getAncestorWithEventId(int eventId, Window endWindow) {
        Window window = this;
        while (window != null) {
            if (window.hasEventListenerFor(eventId)) return window;
            if (window == endWindow || window.attributes.getDoNotPropagateMask().isSet(eventId)) return null;
            window = window.parent;
        }
        return null;
    }

    public boolean isAncestorOf(Window window) {
        if (window == this) return false;
        while (window != null) {
            if (window == this) return true;
            window = window.parent;
        }
        return false;
    }

    public Window getChildByCoords(short x, short y) {
        for (int i = children.size()-1; i >= 0; i--) {
            Window child = children.get(i);
            if (child.attributes.isMapped() && child.containsPoint(x, y)) return child;
        }
        return null;
    }

    public MapState getMapState() {
        if (!attributes.isMapped()) return MapState.UNMAPPED;
        Window window = this;
        do {
            window = window.parent;
            if (window == null) return MapState.VIEWABLE;
        }
        while (window.attributes.isMapped());
        return MapState.UNVIEWABLE;
    }

    public Bitmask getAllEventMasks() {
        Bitmask eventMask = new Bitmask();
        for (EventListener eventListener : eventListeners) eventMask.join(eventListener.eventMask);
        return eventMask;
    }

    public EventListener getButtonPressListener() {
        for (EventListener eventListener : eventListeners) {
            if (eventListener.isInterestedIn(Event.BUTTON_PRESS)) return eventListener;
        }
        return null;
    }

    public void disableAllDescendants() {
        Stack<Window> stack = new Stack<>();
        stack.push(this);
        while (!stack.isEmpty()) {
            Window window = stack.pop();
            window.attributes.setEnabled(false);
            stack.addAll(window.children);
        }
    }

    public String serializeProperties() {
        String result = "";
        for (int i = 0; i < properties.size(); i++) {
            Property property = properties.valueAt(i);
            result += property.nameAsString()+"="+property+"\n";
        }
        return result;
    }
}
