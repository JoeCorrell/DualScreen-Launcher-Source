import { afterEach, describe, expect, it, vi } from "vitest";
import { fetchPatreonIdentity } from "./patreon";
import type { Env } from "./types";

const policy = {
  campaignId: "campaign-1",
  qualifyingTierIds: new Set(["tier-paid"]),
  minimumPledgeCents: 100,
};

describe("Patreon identity request", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("explicitly includes the campaign and tier relationships used for verification", async () => {
    const fetchMock = vi.fn(async (input: string | URL | Request) => {
      const url = new URL(input instanceof Request ? input.url : input.toString());
      expect(url.searchParams.get("include")).toBe(
        "memberships.campaign,memberships.currently_entitled_tiers",
      );
      expect(url.searchParams.get("fields[member]")).toBe(
        "patron_status,last_charge_status,currently_entitled_amount_cents",
      );

      return Response.json({
        data: { type: "user", id: "patron-1", attributes: { full_name: "Patron" } },
        included: [{
          type: "member",
          id: "member-1",
          attributes: {
            patron_status: "active_patron",
            last_charge_status: "Paid",
            currently_entitled_amount_cents: 500,
          },
          relationships: {
            campaign: { data: { type: "campaign", id: "campaign-1" } },
            currently_entitled_tiers: { data: [{ type: "tier", id: "tier-paid" }] },
          },
        }],
      });
    });
    vi.stubGlobal("fetch", fetchMock);

    const membership = await fetchPatreonIdentity({} as Env, "access-token", policy);

    expect(membership.qualifying).toBe(true);
    expect(fetchMock).toHaveBeenCalledOnce();
  });
});
