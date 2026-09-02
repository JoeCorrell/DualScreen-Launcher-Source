import { describe, expect, it } from "vitest";
import {
  displayAccessCode,
  encodeAccessCode,
  normalizeAccessCode,
  parsePatreonIdentity,
} from "./domain";

const policy = {
  campaignId: "campaign-1",
  qualifyingTierIds: new Set(["tier-paid"]),
  minimumPledgeCents: 100,
};

function identity(status = "active_patron", charge = "Paid", tier = "tier-paid") {
  return {
    data: { type: "user", id: "patron-1", attributes: { full_name: "Patron" } },
    included: [{
      type: "member",
      id: "member-1",
      attributes: {
        patron_status: status,
        last_charge_status: charge,
        currently_entitled_amount_cents: 500,
      },
      relationships: {
        campaign: { data: { type: "campaign", id: "campaign-1" } },
        currently_entitled_tiers: { data: [{ type: "tier", id: tier }] },
      },
    }],
  };
}

describe("Patreon qualification", () => {
  it("accepts only an active member in the configured campaign and tier", () => {
    expect(parsePatreonIdentity(identity(), policy).qualifying).toBe(true);
    expect(parsePatreonIdentity(identity("former_patron"), policy).qualifying).toBe(false);
    expect(parsePatreonIdentity(identity("active_patron", "Declined"), policy).qualifying).toBe(false);
    expect(parsePatreonIdentity(identity("active_patron", "Pending"), policy).qualifying).toBe(false);
    expect(parsePatreonIdentity(identity("active_patron", "Partially Refunded"), policy).qualifying).toBe(false);
    expect(parsePatreonIdentity(identity("active_patron", "Free Trial"), policy).qualifying).toBe(false);
    expect(parsePatreonIdentity(identity("active_patron", "Paid", "other-tier"), policy).qualifying).toBe(false);
  });

  it("rejects a membership from another campaign", () => {
    const value = identity();
    value.included[0]!.relationships.campaign.data.id = "other";
    expect(parsePatreonIdentity(value, policy).qualifying).toBe(false);
  });

  it("supports a minimum current entitlement policy when no tiers are configured", () => {
    const amountPolicy = { ...policy, qualifyingTierIds: new Set<string>(), minimumPledgeCents: 600 };
    expect(parsePatreonIdentity(identity(), amountPolicy).qualifying).toBe(false);
    amountPolicy.minimumPledgeCents = 500;
    expect(parsePatreonIdentity(identity(), amountPolicy).qualifying).toBe(true);
  });
});

describe("one-time code canonical form", () => {
  it("round trips the displayed format without ambiguous characters", () => {
    const normalized = encodeAccessCode(new Uint8Array(20).fill(0xab));
    const displayed = displayAccessCode(normalized);
    expect(normalizeAccessCode(displayed)).toBe(normalized);
    expect(normalized).toHaveLength(32);
    expect(normalized).not.toMatch(/[ILOU]/);
  });

  it("rejects short or non-string input", () => {
    expect(normalizeAccessCode("DSL-1234")).toBeNull();
    expect(normalizeAccessCode(1234)).toBeNull();
  });
});
