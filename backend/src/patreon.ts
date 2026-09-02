import { parsePatreonIdentity, type QualificationPolicy } from "./domain";
import type { Env, PatreonMembership, PatreonTokenSet } from "./types";

const PATREON_TOKEN_URL = "https://www.patreon.com/api/oauth2/token";
const PATREON_IDENTITY_URL = "https://www.patreon.com/api/oauth2/v2/identity";

export class PatreonRequestError extends Error {
  constructor(
    message: string,
    readonly reauthorizationRequired: boolean,
    readonly transient: boolean,
    readonly status: number,
  ) {
    super(message);
  }
}

function userAgent(): string {
  return "DualScreen Launcher - Entitlement Service";
}

async function tokenRequest(env: Env, parameters: URLSearchParams): Promise<PatreonTokenSet> {
  parameters.set("client_id", env.PATREON_CLIENT_ID);
  parameters.set("client_secret", env.PATREON_CLIENT_SECRET);
  const response = await fetch(PATREON_TOKEN_URL, {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "User-Agent": userAgent(),
      Accept: "application/json",
    },
    body: parameters.toString(),
  });
  if (!response.ok) {
    throw new PatreonRequestError(
      `Patreon token request failed (${response.status})`,
      response.status === 400 || response.status === 401,
      response.status === 429 || response.status >= 500,
      response.status,
    );
  }
  const parsed = await response.json<Partial<PatreonTokenSet>>();
  if (!parsed.access_token || !parsed.refresh_token || !Number.isFinite(parsed.expires_in)) {
    throw new PatreonRequestError("Patreon returned an incomplete token response", true, false, 502);
  }
  return parsed as PatreonTokenSet;
}

export function exchangeAuthorizationCode(
  env: Env,
  code: string,
  redirectUri: string,
): Promise<PatreonTokenSet> {
  return tokenRequest(env, new URLSearchParams({
    code,
    grant_type: "authorization_code",
    redirect_uri: redirectUri,
  }));
}

export function refreshPatreonToken(env: Env, refreshToken: string): Promise<PatreonTokenSet> {
  return tokenRequest(env, new URLSearchParams({
    grant_type: "refresh_token",
    refresh_token: refreshToken,
  }));
}

export async function fetchPatreonIdentity(
  env: Env,
  accessToken: string,
  policy: QualificationPolicy,
): Promise<PatreonMembership> {
  const url = new URL(PATREON_IDENTITY_URL);
  // Patreon returns the user's membership to the OAuth client's own campaign
  // when `memberships` is requested. We deliberately do not request
  // identity.memberships, which would expose memberships to unrelated creators.
  url.searchParams.set("include", "memberships");
  url.searchParams.set("fields[user]", "full_name");
  url.searchParams.set(
    "fields[member]",
    "patron_status,last_charge_status,currently_entitled_amount_cents",
  );

  const response = await fetch(url, {
    headers: {
      Authorization: `Bearer ${accessToken}`,
      "User-Agent": userAgent(),
      Accept: "application/json",
    },
  });
  if (!response.ok) {
    throw new PatreonRequestError(
      `Patreon identity verification failed (${response.status})`,
      response.status === 401 || response.status === 403,
      response.status === 429 || response.status >= 500,
      response.status,
    );
  }
  return parsePatreonIdentity(await response.json(), policy);
}
