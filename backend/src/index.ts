import {
  constantTimeSecretEqual,
  hmacSha256Hex,
  openSecret,
  randomToken,
  sealSecret,
  sha256Hex,
  signAccessLease,
} from "./crypto";
import {
  displayAccessCode,
  encodeAccessCode,
  normalizeAccessCode,
  type QualificationPolicy,
} from "./domain";
import {
  exchangeAuthorizationCode,
  fetchPatreonIdentity,
  PatreonRequestError,
  refreshPatreonToken,
} from "./patreon";
import type {
  AccessLeaseClaims,
  Env,
  InstallationSession,
  PatreonMembership,
  PatreonTokenSet,
} from "./types";

const JSON_HEADERS = {
  "Content-Type": "application/json; charset=utf-8",
  "Cache-Control": "no-store, max-age=0",
  Pragma: "no-cache",
  "X-Content-Type-Options": "nosniff",
  "X-Frame-Options": "DENY",
  "Content-Security-Policy": "default-src 'none'; frame-ancestors 'none'; base-uri 'none'",
  "Cross-Origin-Resource-Policy": "same-origin",
  "Referrer-Policy": "no-referrer",
  "Strict-Transport-Security": "max-age=31536000",
} as const;
const INSTALLATION_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const OAUTH_SESSION_SECONDS = 10 * 60;
const CODE_FAILURE_WINDOW_SECONDS = 15 * 60;
const MAX_CODE_FAILURES = 10;

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), { status, headers: JSON_HEADERS });
}

function error(code: string, message: string, status: number): Response {
  return json({ error: code, message }, status);
}

function unixNow(): number {
  return Math.floor(Date.now() / 1000);
}

function boundedInteger(value: string, fallback: number, minimum: number, maximum: number): number {
  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) ? Math.min(maximum, Math.max(minimum, parsed)) : fallback;
}

function policy(env: Env): QualificationPolicy {
  const qualifyingTierIds = new Set(
    env.PATREON_TIER_IDS.split(",").map((value) => value.trim()).filter(Boolean),
  );
  return {
    campaignId: env.PATREON_CAMPAIGN_ID.trim(),
    qualifyingTierIds,
    minimumPledgeCents: boundedInteger(env.MIN_PLEDGE_CENTS, 100, 1, 10_000_000),
  };
}

function publicBaseUrl(env: Env): string {
  const value = env.PUBLIC_BASE_URL.trim().replace(/\/$/, "");
  const parsed = new URL(value);
  if (parsed.protocol !== "https:" || parsed.username || parsed.password || parsed.search ||
      parsed.hash || parsed.pathname !== "/") {
    throw new Error("PUBLIC_BASE_URL must be a clean HTTPS origin");
  }
  return value;
}

async function readJson(request: Request): Promise<Record<string, unknown>> {
  if (!request.headers.get("content-type")?.toLowerCase().startsWith("application/json")) {
    throw new Response(JSON.stringify({ error: "json_required", message: "A JSON request body is required." }), {
      status: 415,
      headers: JSON_HEADERS,
    });
  }
  const declaredLength = Number.parseInt(request.headers.get("content-length") ?? "0", 10);
  if (Number.isFinite(declaredLength) && declaredLength > 16_384) {
    throw error("request_too_large", "The request body is too large.", 413);
  }
  const text = await request.text();
  if (text.length > 16_384) throw error("request_too_large", "The request body is too large.", 413);
  const parsed = (() => {
    try {
      return JSON.parse(text) as unknown;
    } catch {
      throw error("invalid_json", "The request body must be valid JSON.", 400);
    }
  })();
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw error("invalid_json", "The request body must be a JSON object.", 400);
  }
  return parsed as Record<string, unknown>;
}

function bearerToken(request: Request): string | null {
  const value = request.headers.get("authorization") ?? "";
  return value.startsWith("Bearer ") ? value.slice(7).trim() : null;
}

async function enforcePublicRateLimit(
  request: Request,
  env: Env,
  scope: string,
): Promise<Response | null> {
  // Cloudflare overwrites CF-Connecting-IP on public Worker requests. The
  // pathname scope avoids one endpoint consuming another endpoint's budget.
  const clientAddress = request.headers.get("CF-Connecting-IP") ?? "unknown-client";
  const result = await env.PUBLIC_RATE_LIMITER.limit({ key: `${scope}:${clientAddress}` });
  return result.success
    ? null
    : error("too_many_requests", "Too many requests. Wait a minute and try again.", 429);
}

async function enforceSessionRateLimit(request: Request, env: Env): Promise<Response | null> {
  const token = bearerToken(request);
  if (!token) return null;
  const result = await env.SESSION_RATE_LIMITER.limit({ key: await sha256Hex(token) });
  return result.success
    ? null
    : error("too_many_requests", "Too many requests. Wait a minute and try again.", 429);
}

async function installationSession(request: Request, env: Env): Promise<InstallationSession | null> {
  const token = bearerToken(request);
  if (!token || !token.startsWith("dsl_session_") || token.length > 128) return null;
  const hash = await sha256Hex(token);
  const row = await env.DB.prepare(`
      SELECT i.id AS installation_id, i.user_id, u.patreon_user_id
      FROM installations i
      LEFT JOIN users u ON u.id = i.user_id
      WHERE i.session_token_hash = ? AND i.revoked_at IS NULL
    `).bind(hash).first<{ installation_id: string; user_id: string | null; patreon_user_id: string | null }>();
  if (!row) return null;
  await env.DB.prepare("UPDATE installations SET last_seen_at = ? WHERE id = ?")
    .bind(unixNow(), row.installation_id).run();
  return {
    installationId: row.installation_id,
    userId: row.user_id,
    patreonUserId: row.patreon_user_id,
  };
}

async function requireInstallation(request: Request, env: Env): Promise<InstallationSession | Response> {
  return await installationSession(request, env) ??
    error("invalid_session", "This installation session is missing or no longer valid.", 401);
}

async function requireAdmin(request: Request, env: Env): Promise<Response | null> {
  const token = bearerToken(request);
  if (!token || !(await constantTimeSecretEqual(token, env.ADMIN_API_TOKEN))) {
    return error("not_found", "Not found.", 404);
  }
  return null;
}

async function audit(
  env: Env,
  action: string,
  session?: InstallationSession | null,
  objectId?: string | null,
  detail?: string | null,
): Promise<void> {
  await env.DB.prepare(`
      INSERT INTO audit_log(id, created_at, action, installation_id, user_id, object_id, detail)
      VALUES (?, ?, ?, ?, ?, ?, ?)
    `).bind(
      randomToken("audit_", 18),
      unixNow(),
      action,
      session?.installationId ?? null,
      session?.userId ?? null,
      objectId ?? null,
      detail?.slice(0, 500) ?? null,
    ).run();
}

async function registerInstallation(request: Request, env: Env): Promise<Response> {
  const limited = await enforcePublicRateLimit(request, env, "register");
  if (limited) return limited;
  const body = await readJson(request);
  const installationId = typeof body.installation_id === "string" ? body.installation_id.trim() : "";
  if (!INSTALLATION_ID.test(installationId)) {
    return error("invalid_installation", "The installation identifier is invalid.", 400);
  }
  const token = randomToken("dsl_session_", 32);
  const now = unixNow();
  const result = await env.DB.prepare(`
      INSERT OR IGNORE INTO installations(id, session_token_hash, created_at, last_seen_at)
      VALUES (?, ?, ?, ?)
    `).bind(installationId, await sha256Hex(token), now, now).run();
  if ((result.meta.changes ?? 0) !== 1) {
    return error(
      "installation_exists",
      "This installation is already registered. Restore its secure session or reinstall to create a new identity.",
      409,
    );
  }
  await audit(env, "installation_registered", { installationId, userId: null, patreonUserId: null });
  return json({ installation_id: installationId, session_token: token }, 201);
}

async function beginOAuth(request: Request, env: Env): Promise<Response> {
  const session = await requireInstallation(request, env);
  if (session instanceof Response) return session;
  const now = unixNow();
  const state = randomToken("dsl_oauth_", 32);
  await env.DB.batch([
    env.DB.prepare(`
        DELETE FROM oauth_sessions
        WHERE expires_at < ? OR completed_at IS NOT NULL OR installation_id = ?
      `).bind(now, session.installationId),
    env.DB.prepare(`
        INSERT INTO oauth_sessions(state_hash, installation_id, created_at, expires_at)
        VALUES (?, ?, ?, ?)
      `).bind(await sha256Hex(state), session.installationId, now, now + OAUTH_SESSION_SECONDS),
  ]);

  const redirectUri = `${publicBaseUrl(env)}/v1/oauth/callback`;
  const authorization = new URL("https://www.patreon.com/oauth2/authorize");
  authorization.searchParams.set("response_type", "code");
  authorization.searchParams.set("client_id", env.PATREON_CLIENT_ID);
  authorization.searchParams.set("redirect_uri", redirectUri);
  // The own-campaign membership is included with the base identity scope. Do
  // not request email or the user's memberships to unrelated campaigns.
  authorization.searchParams.set("scope", "identity");
  authorization.searchParams.set("state", state);
  await audit(env, "patreon_oauth_started", session);
  return json({ authorization_url: authorization.toString(), expires_in: OAUTH_SESSION_SECONDS });
}

function callbackRedirect(
  env: Env,
  result: "complete" | "denied" | "error",
  completionToken?: string,
): Response {
  const callback = new URL(env.APP_CALLBACK_URI);
  if (callback.protocol !== "dualscreen:" || callback.hostname !== "auth" || callback.pathname !== "/callback") {
    throw new Error("APP_CALLBACK_URI must be dualscreen://auth/callback");
  }
  callback.searchParams.set("result", result);
  if (completionToken) callback.searchParams.set("completion_token", completionToken);
  return new Response(null, {
    status: 302,
    headers: { ...JSON_HEADERS, Location: callback.toString() },
  });
}

async function storeMembership(
  env: Env,
  userId: string,
  membership: PatreonMembership,
  verifiedAt: number,
): Promise<D1PreparedStatement> {
  return env.DB.prepare(`
      INSERT INTO patreon_memberships(
        user_id, campaign_id, member_id, patron_status, last_charge_status,
        entitled_amount_cents, tier_ids_json, qualifying, verified_at, verification_error
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)
      ON CONFLICT(user_id) DO UPDATE SET
        campaign_id = excluded.campaign_id,
        member_id = excluded.member_id,
        patron_status = excluded.patron_status,
        last_charge_status = excluded.last_charge_status,
        entitled_amount_cents = excluded.entitled_amount_cents,
        tier_ids_json = excluded.tier_ids_json,
        qualifying = excluded.qualifying,
        verified_at = excluded.verified_at,
        verification_error = NULL
    `).bind(
      userId,
      membership.campaignId,
      membership.memberId,
      membership.patronStatus,
      membership.lastChargeStatus,
      membership.entitledAmountCents,
      JSON.stringify(membership.tierIds),
      membership.qualifying ? 1 : 0,
      verifiedAt,
    );
}

async function oauthCallback(request: Request, env: Env): Promise<Response> {
  const limited = await enforcePublicRateLimit(request, env, "oauth-callback");
  if (limited) return limited;
  const url = new URL(request.url);
  const state = url.searchParams.get("state") ?? "";
  if (!state.startsWith("dsl_oauth_") || state.length > 128) return callbackRedirect(env, "error");
  const stateHash = await sha256Hex(state);
  const sessionRow = await env.DB.prepare(`
      SELECT installation_id, expires_at, consumed_at
      FROM oauth_sessions WHERE state_hash = ?
    `).bind(stateHash).first<{ installation_id: string; expires_at: number; consumed_at: number | null }>();
  if (!sessionRow || sessionRow.consumed_at || sessionRow.expires_at < unixNow()) {
    return callbackRedirect(env, "error");
  }
  // Claim the OAuth state before using its authorization code. Concurrent or
  // replayed callbacks cannot both link an account, even if they raced the
  // read above. A failed exchange requires a new sign-in attempt.
  const claimed = await env.DB.prepare(`
      UPDATE oauth_sessions SET consumed_at = ?
      WHERE state_hash = ? AND consumed_at IS NULL AND expires_at >= ?
    `).bind(unixNow(), stateHash, unixNow()).run();
  if ((claimed.meta.changes ?? 0) !== 1) return callbackRedirect(env, "error");
  if (url.searchParams.has("error")) {
    return callbackRedirect(env, "denied");
  }
  const code = url.searchParams.get("code");
  if (!code || code.length > 1024) return callbackRedirect(env, "error");

  try {
    const now = unixNow();
    const redirectUri = `${publicBaseUrl(env)}/v1/oauth/callback`;
    const tokens = await exchangeAuthorizationCode(env, code, redirectUri);
    const membership = await fetchPatreonIdentity(env, tokens.access_token, policy(env));
    const userId = `user_${(await sha256Hex(membership.patreonUserId)).slice(0, 40)}`;
    const accessCiphertext = await sealSecret(env, tokens.access_token, `${userId}:patreon-access`);
    const refreshCiphertext = await sealSecret(env, tokens.refresh_token, `${userId}:patreon-refresh`);
    const membershipStatement = await storeMembership(env, userId, membership, now);
    // This secret is created only after Patreon has authenticated the user and
    // is delivered through the callback to the device that completed sign-in.
    // OAuth state alone is not proof that the original installation still has
    // control of the browser flow because an authorization URL can be shared.
    const completionToken = randomToken("dsl_oauth_complete_", 32);
    const completionTokenHash = await sha256Hex(completionToken);
    const auditId = randomToken("audit_", 18);

    const results = await env.DB.batch([
      env.DB.prepare(`
          INSERT INTO users(id, patreon_user_id, display_name, email, created_at, updated_at)
          VALUES (?, ?, ?, ?, ?, ?)
          ON CONFLICT(patreon_user_id) DO UPDATE SET
            display_name = excluded.display_name,
            email = excluded.email,
            updated_at = excluded.updated_at
        `).bind(userId, membership.patreonUserId, membership.displayName, membership.email, now, now),
      env.DB.prepare(`
          INSERT INTO oauth_credentials(
            user_id, access_token_ciphertext, refresh_token_ciphertext,
            access_token_expires_at, scopes, revoked_at, updated_at
          ) VALUES (?, ?, ?, ?, ?, NULL, ?)
          ON CONFLICT(user_id) DO UPDATE SET
            access_token_ciphertext = excluded.access_token_ciphertext,
            refresh_token_ciphertext = excluded.refresh_token_ciphertext,
            access_token_expires_at = excluded.access_token_expires_at,
            scopes = excluded.scopes,
            revoked_at = NULL,
            updated_at = excluded.updated_at
        `).bind(
          userId,
          accessCiphertext,
          refreshCiphertext,
          now + Math.max(60, Math.floor(tokens.expires_in)),
          tokens.scope ?? "",
          now,
        ),
      membershipStatement,
      env.DB.prepare(`
          UPDATE oauth_sessions SET
            pending_user_id = ?, completion_token_hash = ?, authorized_at = ?
          WHERE state_hash = ? AND consumed_at IS NOT NULL
            AND completed_at IS NULL AND expires_at >= ?
        `).bind(userId, completionTokenHash, now, stateHash, now),
      env.DB.prepare(`
          INSERT INTO audit_log(id, created_at, action, installation_id, user_id, detail)
          VALUES (?, ?, 'patreon_oauth_authorized', ?, ?, ?)
        `).bind(auditId, now, sessionRow.installation_id, userId, membership.qualifying ? "qualifying" : "not_qualifying"),
    ]);
    if ((results[3]?.meta.changes ?? 0) !== 1) return callbackRedirect(env, "error");
    return callbackRedirect(env, "complete", completionToken);
  } catch (failure) {
    console.error("Patreon OAuth callback failed", failure instanceof Error ? failure.message : "unknown error");
    return callbackRedirect(env, "error");
  }
}

async function completeOAuth(request: Request, env: Env): Promise<Response> {
  const installation = await requireInstallation(request, env);
  if (installation instanceof Response) return installation;
  const body = await readJson(request);
  const completionToken = typeof body.completion_token === "string"
    ? body.completion_token.trim()
    : "";
  if (!completionToken.startsWith("dsl_oauth_complete_") || completionToken.length > 128) {
    return error("invalid_oauth_completion", "Patreon sign-in could not be securely completed. Start sign-in again.", 400);
  }

  const now = unixNow();
  const completionTokenHash = await sha256Hex(completionToken);
  const pending = await env.DB.prepare(`
      SELECT state_hash, pending_user_id
      FROM oauth_sessions
      WHERE completion_token_hash = ? AND installation_id = ?
        AND pending_user_id IS NOT NULL AND authorized_at IS NOT NULL
        AND completed_at IS NULL AND expires_at >= ?
    `).bind(completionTokenHash, installation.installationId, now)
    .first<{ state_hash: string; pending_user_id: string }>();
  if (!pending) {
    return error("invalid_oauth_completion", "Patreon sign-in could not be securely completed. Start sign-in again.", 400);
  }

  const completionNonce = randomToken("complete_", 18);
  const results = await env.DB.batch([
    env.DB.prepare(`
        UPDATE oauth_sessions SET
          completed_at = ?, completion_nonce = ?, completion_token_hash = NULL
        WHERE state_hash = ? AND completion_token_hash = ?
          AND installation_id = ? AND pending_user_id = ?
          AND completed_at IS NULL AND expires_at >= ?
      `).bind(
        now,
        completionNonce,
        pending.state_hash,
        completionTokenHash,
        installation.installationId,
        pending.pending_user_id,
        now,
      ),
    env.DB.prepare(`
        UPDATE installations SET user_id = ?, last_seen_at = ?
        WHERE id = ? AND revoked_at IS NULL AND EXISTS (
          SELECT 1 FROM oauth_sessions
          WHERE state_hash = ? AND completion_nonce = ? AND completed_at = ?
        )
      `).bind(
        pending.pending_user_id,
        now,
        installation.installationId,
        pending.state_hash,
        completionNonce,
        now,
      ),
    env.DB.prepare(`
        INSERT INTO audit_log(id, created_at, action, installation_id, user_id)
        SELECT ?, ?, 'patreon_oauth_completed', ?, ?
        WHERE EXISTS (
          SELECT 1 FROM oauth_sessions
          WHERE state_hash = ? AND completion_nonce = ? AND completed_at = ?
        )
      `).bind(
        randomToken("audit_", 18),
        now,
        installation.installationId,
        pending.pending_user_id,
        pending.state_hash,
        completionNonce,
        now,
      ),
  ]);
  if ((results[0]?.meta.changes ?? 0) !== 1 || (results[1]?.meta.changes ?? 0) !== 1) {
    return error("invalid_oauth_completion", "Patreon sign-in could not be securely completed. Start sign-in again.", 400);
  }
  return json({ completed: true });
}

interface CredentialRow {
  access_token_ciphertext: string;
  refresh_token_ciphertext: string;
  access_token_expires_at: number;
  scopes: string;
  revoked_at: number | null;
}

async function readCredentials(env: Env, userId: string): Promise<CredentialRow | null> {
  return env.DB.prepare(`
      SELECT access_token_ciphertext, refresh_token_ciphertext,
             access_token_expires_at, scopes, revoked_at
      FROM oauth_credentials WHERE user_id = ?
    `).bind(userId).first<CredentialRow>();
}

async function saveRefreshedTokens(
  env: Env,
  userId: string,
  previousRefreshCiphertext: string,
  tokens: PatreonTokenSet,
): Promise<boolean> {
  const now = unixNow();
  const result = await env.DB.prepare(`
      UPDATE oauth_credentials SET
        access_token_ciphertext = ?, refresh_token_ciphertext = ?,
        access_token_expires_at = ?, scopes = ?, revoked_at = NULL, updated_at = ?
      WHERE user_id = ? AND refresh_token_ciphertext = ? AND revoked_at IS NULL
    `).bind(
      await sealSecret(env, tokens.access_token, `${userId}:patreon-access`),
      await sealSecret(env, tokens.refresh_token, `${userId}:patreon-refresh`),
      now + Math.max(60, Math.floor(tokens.expires_in)),
      tokens.scope ?? "",
      now,
      userId,
      previousRefreshCiphertext,
    ).run();
  return (result.meta.changes ?? 0) === 1;
}

async function refreshedAccessToken(
  env: Env,
  userId: string,
  credentials: CredentialRow,
): Promise<string> {
  const refreshToken = await openSecret(env, credentials.refresh_token_ciphertext, `${userId}:patreon-refresh`);
  let tokens: PatreonTokenSet;
  try {
    tokens = await refreshPatreonToken(env, refreshToken);
  } catch (failure) {
    // Multiple installations for one account can refresh concurrently. If a
    // different request already rotated and saved the refresh token, use its
    // newly stored access token instead of incorrectly revoking the account.
    const latest = await readCredentials(env, userId);
    if (latest && !latest.revoked_at &&
        latest.refresh_token_ciphertext !== credentials.refresh_token_ciphertext) {
      return openSecret(env, latest.access_token_ciphertext, `${userId}:patreon-access`);
    }
    throw failure;
  }
  if (await saveRefreshedTokens(env, userId, credentials.refresh_token_ciphertext, tokens)) {
    return tokens.access_token;
  }
  const latest = await readCredentials(env, userId);
  if (latest && !latest.revoked_at &&
      latest.refresh_token_ciphertext !== credentials.refresh_token_ciphertext) {
    return openSecret(env, latest.access_token_ciphertext, `${userId}:patreon-access`);
  }
  throw new PatreonRequestError("Patreon token refresh conflicted with another request", false, true, 503);
}

async function verifyPatreonMembership(
  env: Env,
  session: InstallationSession,
): Promise<PatreonMembership> {
  if (!session.userId || !session.patreonUserId) {
    throw new PatreonRequestError("Patreon account is not linked", true, false, 401);
  }
  const credentials = await readCredentials(env, session.userId);
  if (!credentials || credentials.revoked_at) {
    throw new PatreonRequestError("Patreon authorization has been revoked", true, false, 401);
  }

  let accessToken = credentials.access_token_expires_at <= unixNow() + 60
    ? await refreshedAccessToken(env, session.userId, credentials)
    : await openSecret(env, credentials.access_token_ciphertext, `${session.userId}:patreon-access`);
  let membership: PatreonMembership;
  try {
    membership = await fetchPatreonIdentity(env, accessToken, policy(env));
  } catch (failure) {
    if (!(failure instanceof PatreonRequestError) || !failure.reauthorizationRequired) throw failure;
    accessToken = await refreshedAccessToken(env, session.userId, credentials);
    membership = await fetchPatreonIdentity(env, accessToken, policy(env));
  }
  if (membership.patreonUserId !== session.patreonUserId) {
    throw new PatreonRequestError("Patreon account changed during verification", true, false, 401);
  }
  await (await storeMembership(env, session.userId, membership, unixNow())).run();
  return membership;
}

async function codeEntitlement(
  env: Env,
  session: InstallationSession,
): Promise<{ id: string } | null> {
  return env.DB.prepare(`
      SELECT id FROM entitlements
      WHERE kind = 'code' AND revoked_at IS NULL
        AND ((user_id IS NOT NULL AND user_id = ?)
          OR (installation_id IS NOT NULL AND installation_id = ?))
      ORDER BY granted_at ASC LIMIT 1
    `).bind(session.userId, session.installationId).first<{ id: string }>();
}

async function grantedResponse(
  env: Env,
  session: InstallationSession,
  kind: "patreon" | "code",
  entitlementId: string,
): Promise<Response> {
  const now = unixNow();
  const leaseSeconds = boundedInteger(env.LEASE_SECONDS, 3600, 300, 86_400);
  const claims: AccessLeaseClaims = {
    iss: publicBaseUrl(env),
    aud: "dualscreen-launcher",
    sub: session.userId ?? session.installationId,
    installation_id: session.installationId,
    entitlement: kind,
    entitlement_id: entitlementId,
    iat: now,
    exp: now + leaseSeconds,
    jti: randomToken("lease_", 18),
  };
  return json({
    allowed: true,
    source: kind,
    message: kind === "patreon"
      ? "Active Patreon membership verified."
      : "Your access-code entitlement is active.",
    lease: await signAccessLease(env, claims),
    expires_at: claims.exp,
    account_linked: session.userId !== null,
  });
}

async function entitlementStatus(request: Request, env: Env): Promise<Response> {
  const session = await requireInstallation(request, env);
  if (session instanceof Response) return session;

  const code = await codeEntitlement(env, session);
  if (code) return grantedResponse(env, session, "code", code.id);
  if (!session.userId) {
    return json({
      allowed: false,
      reason: "sign_in_required",
      message: "Sign in with Patreon or redeem a one-time access code to continue.",
      account_linked: false,
    }, 403);
  }

  try {
    const membership = await verifyPatreonMembership(env, session);
    if (!membership.qualifying) {
      await audit(env, "patreon_access_denied", session, membership.memberId, membership.patronStatus);
      return json({
        allowed: false,
        reason: "membership_required",
        message: "An active qualifying Patreon membership is required. Inactive, cancelled, declined, or non-qualifying memberships cannot access the app.",
        account_linked: true,
      }, 403);
    }
    return grantedResponse(env, session, "patreon", membership.memberId ?? `campaign:${membership.campaignId}`);
  } catch (failure) {
    if (failure instanceof PatreonRequestError && failure.reauthorizationRequired) {
      await env.DB.prepare("UPDATE oauth_credentials SET revoked_at = ?, updated_at = ? WHERE user_id = ?")
        .bind(unixNow(), unixNow(), session.userId).run();
      await audit(env, "patreon_reauthorization_required", session, null, failure.message);
      return json({
        allowed: false,
        reason: "reauthorization_required",
        message: "Patreon authorization expired or was revoked. Sign in with Patreon again.",
        account_linked: true,
      }, 401);
    }
    await audit(env, "patreon_verification_unavailable", session, null, failure instanceof Error ? failure.message : null);
    return json({
      allowed: false,
      reason: "verification_unavailable",
      message: "Membership could not be securely verified. Check your connection and try again; access remains locked until verification succeeds.",
      account_linked: true,
    }, 503);
  }
}

async function failedCodeAttempts(env: Env, installationId: string): Promise<number> {
  const row = await env.DB.prepare(`
      SELECT COUNT(*) AS count FROM audit_log
      WHERE installation_id = ? AND action = 'code_redeem_failed' AND created_at >= ?
    `).bind(installationId, unixNow() - CODE_FAILURE_WINDOW_SECONDS).first<{ count: number }>();
  return row?.count ?? 0;
}

async function redeemAccessCode(request: Request, env: Env): Promise<Response> {
  const session = await requireInstallation(request, env);
  if (session instanceof Response) return session;
  if (await failedCodeAttempts(env, session.installationId) >= MAX_CODE_FAILURES) {
    return error("too_many_attempts", "Too many invalid code attempts. Wait 15 minutes and try again.", 429);
  }
  const body = await readJson(request);
  const normalized = normalizeAccessCode(body.code);
  if (!normalized) {
    await audit(env, "code_redeem_failed", session, null, "malformed");
    return error("invalid_or_used_code", "That access code is invalid, expired, revoked, or already redeemed.", 400);
  }

  const now = unixNow();
  const codeHash = await hmacSha256Hex(env.CODE_PEPPER, normalized);
  const entitlementId = randomToken("ent_", 18);
  const nonce = randomToken("redeem_", 18);
  const targetUser = session.userId;
  const targetInstallation = targetUser ? null : session.installationId;
  const results = await env.DB.batch([
    env.DB.prepare(`
        UPDATE access_codes SET
          redeemed_at = ?, redeemed_by_user_id = ?, redeemed_by_installation_id = ?, redemption_nonce = ?
        WHERE code_hash = ? AND redeemed_at IS NULL AND revoked_at IS NULL
          AND (expires_at IS NULL OR expires_at > ?)
      `).bind(now, targetUser, targetInstallation, nonce, codeHash, now),
    env.DB.prepare(`
        INSERT INTO entitlements(id, kind, source_code_id, user_id, installation_id, granted_at)
        SELECT ?, 'code', id, ?, ?, ? FROM access_codes
        WHERE code_hash = ? AND redemption_nonce = ?
      `).bind(entitlementId, targetUser, targetInstallation, now, codeHash, nonce),
  ]);
  const consumed = (results[0]?.meta.changes ?? 0) === 1;
  const granted = (results[1]?.meta.changes ?? 0) === 1;
  if (!consumed || !granted) {
    await audit(env, "code_redeem_failed", session, null, "invalid_or_used");
    return error("invalid_or_used_code", "That access code is invalid, expired, revoked, or already redeemed.", 400);
  }
  await audit(env, "code_redeemed", session, entitlementId, targetUser ? "user" : "installation");
  return grantedResponse(env, session, "code", entitlementId);
}

async function signOut(request: Request, env: Env): Promise<Response> {
  const session = await requireInstallation(request, env);
  if (session instanceof Response) return session;
  await env.DB.prepare("UPDATE installations SET user_id = NULL, last_seen_at = ? WHERE id = ?")
    .bind(unixNow(), session.installationId).run();
  await audit(env, "patreon_account_unlinked", session);
  return json({ signed_out: true });
}

async function createCodes(request: Request, env: Env): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const body = await readJson(request);
  const count = typeof body.count === "number" && Number.isInteger(body.count) ? body.count : 1;
  if (count < 1 || count > 100) return error("invalid_count", "Count must be between 1 and 100.", 400);
  const note = typeof body.note === "string" ? body.note.trim().slice(0, 200) || null : null;
  let expiresAt: number | null = null;
  if (body.expires_at !== undefined && body.expires_at !== null) {
    const parsed = Date.parse(String(body.expires_at));
    if (!Number.isFinite(parsed) || parsed <= Date.now()) {
      return error("invalid_expiry", "expires_at must be a future ISO-8601 timestamp.", 400);
    }
    expiresAt = Math.floor(parsed / 1000);
  }

  const now = unixNow();
  const created: Array<{ id: string; code: string; expires_at: number | null }> = [];
  const statements: D1PreparedStatement[] = [];
  for (let index = 0; index < count; index += 1) {
    const normalized = encodeAccessCode(crypto.getRandomValues(new Uint8Array(20)));
    const id = randomToken("code_", 18);
    const displayed = displayAccessCode(normalized);
    created.push({ id, code: displayed, expires_at: expiresAt });
    statements.push(env.DB.prepare(`
        INSERT INTO access_codes(id, code_hash, code_hint, note, created_at, expires_at)
        VALUES (?, ?, ?, ?, ?, ?)
      `).bind(
        id,
        await hmacSha256Hex(env.CODE_PEPPER, normalized),
        normalized.slice(-4),
        note,
        now,
        expiresAt,
      ));
  }
  await env.DB.batch(statements);
  await audit(env, "admin_codes_created", null, null, `count=${count}`);
  return json({
    warning: "These plaintext codes are returned once and are never stored by the service. Save them securely now.",
    codes: created,
  }, 201);
}

async function listCodes(request: Request, env: Env): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const rows = await env.DB.prepare(`
      SELECT id, code_hint, note, created_at, expires_at, redeemed_at,
             redeemed_by_user_id, redeemed_by_installation_id, revoked_at
      FROM access_codes ORDER BY created_at DESC LIMIT 500
    `).all();
  return json({ codes: rows.results });
}

async function revokeCode(request: Request, env: Env, codeId: string): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const result = await env.DB.prepare(`
      UPDATE access_codes SET revoked_at = ?
      WHERE id = ? AND redeemed_at IS NULL AND revoked_at IS NULL
    `).bind(unixNow(), codeId).run();
  if ((result.meta.changes ?? 0) !== 1) {
    return error("not_revocable", "The code does not exist, is already revoked, or was already redeemed. Revoke its entitlement instead.", 409);
  }
  await audit(env, "admin_code_revoked", null, codeId);
  return json({ revoked: true, code_id: codeId });
}

async function listEntitlements(request: Request, env: Env): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const rows = await env.DB.prepare(`
      SELECT e.id, e.kind, e.source_code_id, e.user_id, e.installation_id,
             e.granted_at, e.revoked_at, e.revoke_reason, c.code_hint
      FROM entitlements e JOIN access_codes c ON c.id = e.source_code_id
      ORDER BY e.granted_at DESC LIMIT 500
    `).all();
  return json({ entitlements: rows.results });
}

async function revokeEntitlement(request: Request, env: Env, entitlementId: string): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const body = await readJson(request);
  const reason = typeof body.reason === "string" ? body.reason.trim().slice(0, 300) : "Revoked by administrator";
  const result = await env.DB.prepare(`
      UPDATE entitlements SET revoked_at = ?, revoke_reason = ?
      WHERE id = ? AND revoked_at IS NULL
    `).bind(unixNow(), reason || "Revoked by administrator", entitlementId).run();
  if ((result.meta.changes ?? 0) !== 1) return error("not_found", "Active entitlement not found.", 404);
  await audit(env, "admin_entitlement_revoked", null, entitlementId, reason);
  return json({ revoked: true, entitlement_id: entitlementId });
}

async function revokeInstallation(request: Request, env: Env, installationId: string): Promise<Response> {
  const denied = await requireAdmin(request, env);
  if (denied) return denied;
  const result = await env.DB.prepare("UPDATE installations SET revoked_at = ? WHERE id = ? AND revoked_at IS NULL")
    .bind(unixNow(), installationId).run();
  if ((result.meta.changes ?? 0) !== 1) return error("not_found", "Active installation not found.", 404);
  await audit(env, "admin_installation_revoked", null, installationId);
  return json({ revoked: true, installation_id: installationId });
}

async function route(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const path = url.pathname.replace(/\/$/, "") || "/";
  if (request.method === "GET" && path === "/health") return json({ status: "ok" });
  if (request.method === "POST" && path === "/v1/installations/register") return registerInstallation(request, env);
  if (path.startsWith("/v1/") && path !== "/v1/oauth/callback") {
    const publicLimited = await enforcePublicRateLimit(request, env, "api");
    if (publicLimited) return publicLimited;
    const sessionLimited = await enforceSessionRateLimit(request, env);
    if (sessionLimited) return sessionLimited;
  }
  if (request.method === "POST" && path === "/v1/oauth/start") return beginOAuth(request, env);
  if (request.method === "GET" && path === "/v1/oauth/callback") return oauthCallback(request, env);
  if (request.method === "POST" && path === "/v1/oauth/complete") return completeOAuth(request, env);
  if (request.method === "POST" && path === "/v1/entitlement/status") return entitlementStatus(request, env);
  if (request.method === "POST" && path === "/v1/access-codes/redeem") return redeemAccessCode(request, env);
  if (request.method === "POST" && path === "/v1/session/logout") return signOut(request, env);
  if (request.method === "POST" && path === "/v1/admin/codes") return createCodes(request, env);
  if (request.method === "GET" && path === "/v1/admin/codes") return listCodes(request, env);
  if (request.method === "GET" && path === "/v1/admin/entitlements") return listEntitlements(request, env);

  const codeRevoke = path.match(/^\/v1\/admin\/codes\/([A-Za-z0-9_-]+)\/revoke$/);
  if (request.method === "POST" && codeRevoke?.[1]) return revokeCode(request, env, codeRevoke[1]);
  const entitlementRevoke = path.match(/^\/v1\/admin\/entitlements\/([A-Za-z0-9_-]+)\/revoke$/);
  if (request.method === "POST" && entitlementRevoke?.[1]) {
    return revokeEntitlement(request, env, entitlementRevoke[1]);
  }
  const installationRevoke = path.match(/^\/v1\/admin\/installations\/([0-9a-f-]+)\/revoke$/i);
  if (request.method === "POST" && installationRevoke?.[1]) {
    return revokeInstallation(request, env, installationRevoke[1]);
  }
  return error("not_found", "Not found.", 404);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method === "OPTIONS") return error("not_found", "Not found.", 404);
    try {
      return await route(request, env);
    } catch (failure) {
      if (failure instanceof Response) return failure;
      console.error("Unhandled entitlement service error", failure instanceof Error ? failure.message : "unknown error");
      return error("internal_error", "The entitlement service could not complete the request.", 500);
    }
  },
} satisfies ExportedHandler<Env>;
