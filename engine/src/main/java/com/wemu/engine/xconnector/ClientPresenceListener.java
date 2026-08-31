package com.wemu.engine.xconnector;

/**
 * Told how many clients are connected to the X server, whenever that changes.
 *
 * The count is the most direct evidence this process has that a guest exists.
 * Every Wine process that can draw opens a connection here and holds it for as
 * long as it lives, so the first connection is the moment the guest became
 * real — which is not something the launcher process can report, because it may
 * return long before or long after.
 *
 * Called from whichever thread noticed the change: the connector's epoll thread
 * for a new connection, and either that thread or the client's own poll thread
 * for a shutdown. An implementation must not assume a single caller, and must
 * not block.
 */
public interface ClientPresenceListener {
    void onClientCountChanged(int count);
}
