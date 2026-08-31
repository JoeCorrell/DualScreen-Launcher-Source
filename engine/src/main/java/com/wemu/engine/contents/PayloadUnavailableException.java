package com.wemu.engine.contents;

import java.io.IOException;

/**
 * A payload that is not in the APK could not be fetched either.
 *
 * Separate from the ordinary extraction failures so the boot screen can say
 * something true. Once the root filesystem and Wine are downloaded rather than
 * bundled, the most likely reason a first install fails is that the device has
 * no network — and telling that user to check their free storage sends them to
 * look at the one thing that is fine.
 */
public class PayloadUnavailableException extends IOException {

    public PayloadUnavailableException(String message) {
        super(message);
    }
}
