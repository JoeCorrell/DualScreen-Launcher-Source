# DualScreen entitlement service

This Worker is the security authority for DualScreen Launcher. The APK never
receives a Patreon client secret, Patreon access/refresh token, access-code
hash, code pepper, administrator token, or lease-signing private key.

Access is granted only when the service can currently prove one of these:

- the linked Patreon identity has an `active_patron` membership in the configured
  campaign, an acceptable charge state, and a configured tier (or minimum current
  entitlement amount); or
- the linked user or installation owns a non-revoked code entitlement.

The Android client checks online on every cold start and periodically while it
is running. Network and Patreon API failures do not mint a lease and therefore
fail closed.

A code redeemed while a Patreon account is linked follows that backend user.
A code redeemed without Patreon is intentionally bound to that installation.
Clearing app data or losing the Keystore key cannot restore an
installation-bound grant, and the consumed code cannot be redeemed again; use
the admin API to issue a replacement when appropriate.

## Security model

- Patreon OAuth authorization codes are sent to this HTTPS service. Only the
  service exchanges them using the client secret.
- OAuth account linking uses a second, one-time completion secret that is
  created only after Patreon authorization, delivered to the returning app,
  and bound to the original installation session. Forwarding a Patreon
  authorization URL cannot link the supporter's account to the sender's app.
- OAuth tokens are encrypted at rest with AES-256-GCM and per-record associated
  data. Refresh-token rotation is persisted after every refresh, with
  optimistic conflict handling for simultaneous checks from multiple devices.
- Installation bearer tokens contain 256 random bits. Only SHA-256 hashes are
  stored in D1, and the Android copy is encrypted with Android Keystore.
- Access codes contain 160 random bits. D1 stores only HMAC-SHA-256 hashes.
- Code consumption and entitlement creation are one D1 transactional batch.
  A random redemption nonce ties the entitlement insert to the one update that
  won the race, so two devices cannot both redeem a code.
- Redeemed codes are never reset. Revoking a code entitlement does not make the
  original code usable again.
- Successful status responses carry a short-lived ES256 lease bound to the
  installation UUID. The APK contains only the corresponding public key.
- The custom-scheme callback never contains a session bearer, Patreon OAuth
  token, access code, or entitlement. Its one-time completion secret is useful
  only with the original installation's separately protected bearer and is
  erased server-side as soon as it is accepted.
- Invalid, expired, revoked, and already-used codes deliberately return the
  same response. Repeated failures are rate-limited per installation.
- Cloudflare edge counters also rate-limit public and bearer-authenticated API
  traffic before it can be used to exhaust D1 or Patreon API capacity.
- Charge states other than `Paid` fail closed. A null charge state qualifies
  only when Patreon simultaneously reports `active_patron` and a current
  qualifying tier or entitlement amount.

No client-side gate can make a locally patched APK impossible. Keep proprietary
or high-value downloadable payloads behind an authenticated private object
store if they must also be inaccessible to modified clients.

## Patreon setup

Create a Patreon API **v2** client and register this exact redirect URI:

```text
https://YOUR_ACCESS_DOMAIN/v1/oauth/callback
```

The service requests only the `identity` scope. Patreon documents that an
identity request with `include=memberships` returns the user's membership to the
OAuth client's own campaign without requesting access to their memberships to
other creators.

Set `PATREON_CAMPAIGN_ID`. Set `PATREON_TIER_IDS` to a comma-separated allowlist
of qualifying tier IDs. If the allowlist is empty, `MIN_PLEDGE_CENTS` is used.

## Create and configure D1

```powershell
cd backend
npm install
npx wrangler d1 create dualscreen-entitlements
```

Copy the returned database ID into `wrangler.toml`, replace all example public
configuration, then apply the migration:

```powershell
npm run db:migrate:remote
```

## Generate the lease key pair

Generate a P-256 key outside the repository:

```powershell
openssl ecparam -name prime256v1 -genkey -noout -out lease-private.pem
openssl pkcs8 -topk8 -nocrypt -in lease-private.pem -outform DER -out lease-private.der
openssl ec -in lease-private.pem -pubout -outform DER -out lease-public.der
```

Encode both DER files using unpadded base64url. Store the private value only in
the Worker secret `LEASE_SIGNING_PRIVATE_KEY`. Pass the public value to the
Android build as `accessLeasePublicKey`; it is intentionally public.

Generate the other secrets with a cryptographically secure password generator.
`TOKEN_ENCRYPTION_KEY` must be exactly 32 random bytes encoded as unpadded
base64url.

```powershell
npx wrangler secret put PATREON_CLIENT_ID
npx wrangler secret put PATREON_CLIENT_SECRET
npx wrangler secret put TOKEN_ENCRYPTION_KEY
npx wrangler secret put CODE_PEPPER
npx wrangler secret put ADMIN_API_TOKEN
npx wrangler secret put LEASE_SIGNING_PRIVATE_KEY
```

Deploy and confirm `https://YOUR_ACCESS_DOMAIN/health`:

```powershell
npm run check
npm test
npm run deploy
```

## Build the Android client

Place these non-secret settings in the user-level Gradle properties file or
pass them on the command line. Do not put them in a signing/secrets file:

```properties
accessApiBaseUrl=https://YOUR_ACCESS_DOMAIN
accessLeasePublicKey=UNPADDED_BASE64URL_X509_PUBLIC_KEY
```

An empty or invalid setting intentionally leaves the APK locked with a
configuration error.

## Create and revoke codes

The helper reads its administrator token from the shell. It never writes it to
disk. Newly created plaintext codes are returned exactly once; D1 stores only
their hashes.

```powershell
$env:ACCESS_API_BASE_URL = "https://YOUR_ACCESS_DOMAIN"
$env:ACCESS_ADMIN_TOKEN = "YOUR_ADMIN_TOKEN"
npm run admin -- create 5 "September supporter codes"
npm run admin -- codes
npm run admin -- entitlements
npm run admin -- revoke-code CODE_ID
npm run admin -- revoke-entitlement ENTITLEMENT_ID "Refunded"
npm run admin -- revoke-installation INSTALLATION_UUID
```

Revoking an unused code prevents redemption. Once a code has been redeemed,
revoke the resulting entitlement; the code remains permanently consumed.

On the configured Windows development machine, the deployment setup stores a
DPAPI-encrypted recovery bundle at
`%LOCALAPPDATA%\DualScreenLauncher\backend-secrets.dpapi`. It is encrypted for
the current Windows account and is never placed in the repository. The local
wrapper decrypts the administrator token only for the lifetime of the command:

```powershell
.\scripts\admin-local.ps1 create 5 "September supporter codes"
.\scripts\admin-local.ps1 codes
.\scripts\admin-local.ps1 entitlements
```

## Operational notes

- Back up D1 and retain the encryption and signing keys. Losing the token key
  forces every Patreon user to authorize again; losing the signing key requires
  an APK public-key rotation.
- Rotate the administrator token and code pepper only with a migration plan.
  Changing the code pepper makes every unredeemed code unverifiable.
- Monitor `audit_log`, Worker errors, D1 health, Patreon 401/429/5xx responses,
  and OAuth callback failures.
- Patreon has no sandbox. Use separate test creator/patron accounts as described
  in Patreon's official testing guidance.

Before distributing a production APK, exercise an active member, inactive
member, revoked OAuth grant, simultaneous two-device redemption attempt,
entitlement revocation, installation revocation, and offline launch against the
deployed service. The expected result is always fail-closed except for the one
transaction winner in the redemption race.
