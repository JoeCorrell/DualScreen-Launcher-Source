package com.wemu.engine.xserver.errors;

public class BadValue extends XRequestError {
    public BadValue(int data) {
        super(2, data);
    }
}
