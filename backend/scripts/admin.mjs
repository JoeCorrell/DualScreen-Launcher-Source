const [command, ...args] = process.argv.slice(2);
const base = (process.env.ACCESS_API_BASE_URL ?? "").replace(/\/$/, "");
const token = process.env.ACCESS_ADMIN_TOKEN ?? "";
if (!base.startsWith("https://") || !token) {
  console.error("Set ACCESS_API_BASE_URL and ACCESS_ADMIN_TOKEN in your shell.");
  process.exit(2);
}

async function call(path, options = {}) {
  const response = await fetch(`${base}${path}`, {
    ...options,
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
      ...(options.headers ?? {}),
    },
  });
  const body = await response.json();
  if (!response.ok) {
    console.error(JSON.stringify(body, null, 2));
    process.exit(1);
  }
  console.log(JSON.stringify(body, null, 2));
}

switch (command) {
  case "create": {
    const count = Number.parseInt(args[0] ?? "1", 10);
    const note = args.slice(1).join(" ") || undefined;
    await call("/v1/admin/codes", { method: "POST", body: JSON.stringify({ count, note }) });
    break;
  }
  case "codes":
    await call("/v1/admin/codes");
    break;
  case "entitlements":
    await call("/v1/admin/entitlements");
    break;
  case "revoke-code":
    if (!args[0]) throw new Error("Usage: admin revoke-code <code-id>");
    await call(`/v1/admin/codes/${encodeURIComponent(args[0])}/revoke`, { method: "POST", body: "{}" });
    break;
  case "revoke-entitlement":
    if (!args[0]) throw new Error("Usage: admin revoke-entitlement <entitlement-id> [reason]");
    await call(`/v1/admin/entitlements/${encodeURIComponent(args[0])}/revoke`, {
      method: "POST",
      body: JSON.stringify({ reason: args.slice(1).join(" ") || "Revoked by administrator" }),
    });
    break;
  case "revoke-installation":
    if (!args[0]) throw new Error("Usage: admin revoke-installation <installation-id>");
    await call(`/v1/admin/installations/${encodeURIComponent(args[0])}/revoke`, { method: "POST", body: "{}" });
    break;
  default:
    console.error("Commands: create [count] [note], codes, entitlements, revoke-code, revoke-entitlement, revoke-installation");
    process.exit(2);
}
