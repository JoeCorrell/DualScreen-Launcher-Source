import type { AccessLeaseClaims, Env } from "./types";

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export function base64UrlEncode(bytes: ArrayBuffer | Uint8Array): string {
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  let binary = "";
  for (const byte of data) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

export function base64UrlDecode(value: string): Uint8Array<ArrayBuffer> {
  const padded = value.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((value.length + 3) % 4);
  const binary = atob(padded);
  const decoded = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) decoded[index] = binary.charCodeAt(index);
  return decoded;
}

export function randomToken(prefix: string, bytes = 32): string {
  const random = crypto.getRandomValues(new Uint8Array(bytes));
  return `${prefix}${base64UrlEncode(random)}`;
}

export async function sha256Hex(value: string): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(value)));
  return [...digest].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function hmacSha256Hex(secret: string, value: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const signature = new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(value)));
  return [...signature].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function constantTimeSecretEqual(left: string, right: string): Promise<boolean> {
  const [a, b] = await Promise.all([sha256Hex(left), sha256Hex(right)]);
  let different = a.length ^ b.length;
  for (let index = 0; index < Math.min(a.length, b.length); index += 1) {
    different |= a.charCodeAt(index) ^ b.charCodeAt(index);
  }
  return different === 0;
}

async function encryptionKey(encoded: string): Promise<CryptoKey> {
  const raw = base64UrlDecode(encoded.trim());
  if (raw.byteLength !== 32) throw new Error("TOKEN_ENCRYPTION_KEY must contain exactly 32 bytes");
  return crypto.subtle.importKey("raw", raw, "AES-GCM", false, ["encrypt", "decrypt"]);
}

export async function sealSecret(env: Env, plaintext: string, associatedData: string): Promise<string> {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ciphertext = await crypto.subtle.encrypt(
    { name: "AES-GCM", iv, additionalData: encoder.encode(associatedData), tagLength: 128 },
    await encryptionKey(env.TOKEN_ENCRYPTION_KEY),
    encoder.encode(plaintext),
  );
  return `v1.${base64UrlEncode(iv)}.${base64UrlEncode(ciphertext)}`;
}

export async function openSecret(env: Env, sealed: string, associatedData: string): Promise<string> {
  const [version, iv, ciphertext] = sealed.split(".");
  if (version !== "v1" || !iv || !ciphertext) throw new Error("Invalid encrypted token envelope");
  const plaintext = await crypto.subtle.decrypt(
    {
      name: "AES-GCM",
      iv: base64UrlDecode(iv),
      additionalData: encoder.encode(associatedData),
      tagLength: 128,
    },
    await encryptionKey(env.TOKEN_ENCRYPTION_KEY),
    base64UrlDecode(ciphertext),
  );
  return decoder.decode(plaintext);
}

export async function signAccessLease(env: Env, claims: AccessLeaseClaims): Promise<string> {
  const header = {
    alg: "ES256",
    typ: "JWT",
    kid: env.LEASE_SIGNING_KEY_ID,
  };
  const encodedHeader = base64UrlEncode(encoder.encode(JSON.stringify(header)));
  const encodedClaims = base64UrlEncode(encoder.encode(JSON.stringify(claims)));
  const signingInput = `${encodedHeader}.${encodedClaims}`;
  const privateKey = await crypto.subtle.importKey(
    "pkcs8",
    base64UrlDecode(env.LEASE_SIGNING_PRIVATE_KEY.trim()),
    { name: "ECDSA", namedCurve: "P-256" },
    false,
    ["sign"],
  );
  const signature = await crypto.subtle.sign(
    { name: "ECDSA", hash: "SHA-256" },
    privateKey,
    encoder.encode(signingInput),
  );
  return `${signingInput}.${base64UrlEncode(signature)}`;
}
