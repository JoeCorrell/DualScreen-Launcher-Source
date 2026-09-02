ALTER TABLE oauth_sessions ADD COLUMN pending_user_id TEXT;
ALTER TABLE oauth_sessions ADD COLUMN completion_token_hash TEXT;
ALTER TABLE oauth_sessions ADD COLUMN authorized_at INTEGER;
ALTER TABLE oauth_sessions ADD COLUMN completed_at INTEGER;
ALTER TABLE oauth_sessions ADD COLUMN completion_nonce TEXT;

CREATE UNIQUE INDEX IF NOT EXISTS idx_oauth_sessions_completion_token
  ON oauth_sessions(completion_token_hash)
  WHERE completion_token_hash IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_oauth_sessions_pending_completion
  ON oauth_sessions(installation_id, expires_at, completed_at);
