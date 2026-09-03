CREATE TABLE reusable_access_codes (
    id TEXT PRIMARY KEY,
    code_hash TEXT NOT NULL UNIQUE,
    code_hint TEXT NOT NULL,
    note TEXT,
    created_at INTEGER NOT NULL,
    expires_at INTEGER,
    redemption_count INTEGER NOT NULL DEFAULT 0 CHECK (redemption_count >= 0),
    revoked_at INTEGER
);
CREATE INDEX reusable_access_codes_state_idx
    ON reusable_access_codes(revoked_at, expires_at);

CREATE TABLE reusable_entitlements (
    id TEXT PRIMARY KEY,
    source_code_id TEXT NOT NULL REFERENCES reusable_access_codes(id),
    user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
    installation_id TEXT REFERENCES installations(id) ON DELETE CASCADE,
    granted_at INTEGER NOT NULL,
    revoked_at INTEGER,
    revoke_reason TEXT,
    CHECK ((user_id IS NOT NULL AND installation_id IS NULL) OR
           (user_id IS NULL AND installation_id IS NOT NULL))
);
CREATE UNIQUE INDEX reusable_entitlements_user_source_idx
    ON reusable_entitlements(source_code_id, user_id)
    WHERE user_id IS NOT NULL;
CREATE UNIQUE INDEX reusable_entitlements_installation_source_idx
    ON reusable_entitlements(source_code_id, installation_id)
    WHERE installation_id IS NOT NULL;
CREATE INDEX reusable_entitlements_user_idx
    ON reusable_entitlements(user_id, revoked_at);
CREATE INDEX reusable_entitlements_installation_idx
    ON reusable_entitlements(installation_id, revoked_at);
