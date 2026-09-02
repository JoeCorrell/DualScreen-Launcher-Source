export interface Env {
  DB: D1Database;
  PUBLIC_RATE_LIMITER: RateLimit;
  SESSION_RATE_LIMITER: RateLimit;
  PUBLIC_BASE_URL: string;
  APP_CALLBACK_URI: string;
  PATREON_CAMPAIGN_ID: string;
  PATREON_TIER_IDS: string;
  MIN_PLEDGE_CENTS: string;
  LEASE_SECONDS: string;
  LEASE_SIGNING_KEY_ID: string;

  // Worker secrets. They must be configured with `wrangler secret put`.
  PATREON_CLIENT_ID: string;
  PATREON_CLIENT_SECRET: string;
  TOKEN_ENCRYPTION_KEY: string;
  CODE_PEPPER: string;
  ADMIN_API_TOKEN: string;
  LEASE_SIGNING_PRIVATE_KEY: string;
}

export interface InstallationSession {
  installationId: string;
  userId: string | null;
  patreonUserId: string | null;
}

export interface PatreonTokenSet {
  access_token: string;
  refresh_token: string;
  expires_in: number;
  scope?: string;
  token_type?: string;
}

export interface PatreonMembership {
  patreonUserId: string;
  displayName: string | null;
  email: string | null;
  campaignId: string;
  memberId: string | null;
  patronStatus: string | null;
  lastChargeStatus: string | null;
  entitledAmountCents: number;
  tierIds: string[];
  qualifying: boolean;
}

export interface AccessLeaseClaims {
  iss: string;
  aud: "dualscreen-launcher";
  sub: string;
  installation_id: string;
  entitlement: "patreon" | "code";
  entitlement_id: string;
  iat: number;
  exp: number;
  jti: string;
}
