package com.wemu.runtime;

/** Receives the final status of the guest process tree. */
@FunctionalInterface
public interface RuntimeTerminationListener {
    void onSessionEnded(int status);
}
