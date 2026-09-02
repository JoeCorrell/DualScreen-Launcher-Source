import type { PatreonMembership } from "./types";

interface JsonApiRelationship {
  data?: { id?: unknown; type?: unknown } | Array<{ id?: unknown; type?: unknown }> | null;
}

interface JsonApiResource {
  id?: unknown;
  type?: unknown;
  attributes?: Record<string, unknown>;
  relationships?: Record<string, JsonApiRelationship>;
}

interface IdentityDocument {
  data?: JsonApiResource;
  included?: JsonApiResource[];
}

export interface QualificationPolicy {
  campaignId: string;
  qualifyingTierIds: Set<string>;
  minimumPledgeCents: number;
}

function stringValue(value: unknown): string | null {
  return typeof value === "string" && value.trim() ? value.trim() : null;
}

function integerValue(value: unknown): number {
  return typeof value === "number" && Number.isSafeInteger(value) ? value : 0;
}

function relationshipIds(resource: JsonApiResource, name: string): string[] {
  const data = resource.relationships?.[name]?.data;
  if (!data) return [];
  const entries = Array.isArray(data) ? data : [data];
  return entries.map((entry) => stringValue(entry.id)).filter((id): id is string => id !== null);
}

export function parsePatreonIdentity(
  raw: unknown,
  policy: QualificationPolicy,
): PatreonMembership {
  const document = (raw ?? {}) as IdentityDocument;
  const user = document.data ?? {};
  const patreonUserId = stringValue(user.id);
  if (!patreonUserId || user.type !== "user") throw new Error("Patreon identity response had no user");

  const memberships = (document.included ?? []).filter((resource) => resource.type === "member");
  const member = memberships.find((candidate) =>
    relationshipIds(candidate, "campaign").includes(policy.campaignId),
  );
  const attributes = member?.attributes ?? {};
  const patronStatus = stringValue(attributes.patron_status);
  const lastChargeStatus = stringValue(attributes.last_charge_status);
  const tierIds = member ? relationshipIds(member, "currently_entitled_tiers") : [];
  const entitledAmountCents = integerValue(attributes.currently_entitled_amount_cents);

  // Patreon currently documents Paid plus several declined, pending, fraud,
  // refund, free-trial, and catch-all states. Fail closed for every explicit
  // state except Paid. A null state is allowed only alongside active_patron
  // and a current tier/amount, covering memberships without a charge record.
  const chargeIsAcceptable = !lastChargeStatus || lastChargeStatus.toLowerCase() === "paid";
  const tierQualifies = policy.qualifyingTierIds.size > 0
    ? tierIds.some((id) => policy.qualifyingTierIds.has(id))
    : entitledAmountCents >= policy.minimumPledgeCents;

  return {
    patreonUserId,
    displayName: stringValue(user.attributes?.full_name),
    email: stringValue(user.attributes?.email),
    campaignId: policy.campaignId,
    memberId: stringValue(member?.id),
    patronStatus,
    lastChargeStatus,
    entitledAmountCents,
    tierIds,
    qualifying: patronStatus === "active_patron" && chargeIsAcceptable && tierQualifies,
  };
}

export function normalizeAccessCode(value: unknown): string | null {
  if (typeof value !== "string") return null;
  const compact = value.toUpperCase().replace(/[^A-Z0-9]/g, "");
  const normalized = compact.startsWith("DSL") ? compact.slice(3) : compact;
  return normalized.length >= 26 && normalized.length <= 40 ? normalized : null;
}

const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

export function encodeAccessCode(bytes: Uint8Array): string {
  let bits = 0;
  let value = 0;
  let output = "";
  for (const byte of bytes) {
    value = (value << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      output += CROCKFORD[(value >>> (bits - 5)) & 31];
      bits -= 5;
    }
  }
  if (bits > 0) output += CROCKFORD[(value << (5 - bits)) & 31];
  return output;
}

export function displayAccessCode(normalized: string): string {
  return `DSL-${normalized.match(/.{1,4}/g)?.join("-") ?? normalized}`;
}
