PRAGMA foreign_keys = ON;

CREATE TABLE users (
    id TEXT PRIMARY KEY,
    patreon_user_id TEXT NOT NULL UNIQUE,
    display_name TEXT,
    email TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE oauth_credentials (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    access_token_ciphertext TEXT NOT NULL,
    refresh_token_ciphertext TEXT NOT NULL,
    access_token_expires_at INTEGER NOT NULL,
    scopes TEXT NOT NULL,
    revoked_at INTEGER,
    updated_at INTEGER NOT NULL
);

CREATE TABLE installations (
    id TEXT PRIMARY KEY,
    session_token_hash TEXT NOT NULL UNIQUE,
    user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    last_seen_at INTEGER NOT NULL,
    revoked_at INTEGER
);
CREATE INDEX installations_user_idx ON installations(user_id);

CREATE TABLE oauth_sessions (
    state_hash TEXT PRIMARY KEY,
    installation_id TEXT NOT NULL REFERENCES installations(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    consumed_at INTEGER
);
CREATE INDEX oauth_sessions_expiry_idx ON oauth_sessions(expires_at);

CREATE TABLE patreon_memberships (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    campaign_id TEXT NOT NULL,
    member_id TEXT,
    patron_status TEXT,
    last_charge_status TEXT,
    entitled_amount_cents INTEGER NOT NULL DEFAULT 0,
    tier_ids_json TEXT NOT NULL DEFAULT '[]',
    qualifying INTEGER NOT NULL DEFAULT 0 CHECK (qualifying IN (0, 1)),
    verified_at INTEGER NOT NULL,
    verification_error TEXT
);

CREATE TABLE access_codes (
    id TEXT PRIMARY KEY,
    code_hash TEXT NOT NULL UNIQUE,
    code_hint TEXT NOT NULL,
    note TEXT,
    created_at INTEGER NOT NULL,
    expires_at INTEGER,
    redeemed_at INTEGER,
    redeemed_by_user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
    redeemed_by_installation_id TEXT REFERENCES installations(id) ON DELETE SET NULL,
    redemption_nonce TEXT,
    revoked_at INTEGER,
    CHECK (redeemed_at IS NULL OR redemption_nonce IS NOT NULL)
);
CREATE INDEX access_codes_state_idx ON access_codes(redeemed_at, revoked_at, expires_at);

CREATE TABLE entitlements (
    id TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK (kind = 'code'),
    source_code_id TEXT NOT NULL UNIQUE REFERENCES access_codes(id),
    user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
    installation_id TEXT REFERENCES installations(id) ON DELETE CASCADE,
    granted_at INTEGER NOT NULL,
    revoked_at INTEGER,
    revoke_reason TEXT,
    CHECK ((user_id IS NOT NULL AND installation_id IS NULL) OR
           (user_id IS NULL AND installation_id IS NOT NULL))
);
CREATE INDEX entitlements_user_idx ON entitlements(user_id, revoked_at);
CREATE INDEX entitlements_installation_idx ON entitlements(installation_id, revoked_at);

CREATE TABLE audit_log (
    id TEXT PRIMARY KEY,
    created_at INTEGER NOT NULL,
    action TEXT NOT NULL,
    installation_id TEXT,
    user_id TEXT,
    object_id TEXT,
    detail TEXT
);
CREATE INDEX audit_installation_action_idx
    ON audit_log(installation_id, action, created_at);
CREATE INDEX audit_created_idx ON audit_log(created_at);
