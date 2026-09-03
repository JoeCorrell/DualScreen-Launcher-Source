import { accessSync, constants, existsSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

const [command, ...args] = process.argv.slice(2);
const base = (process.env.ACCESS_API_BASE_URL ?? "").replace(/\/$/, "");
const token = process.env.ACCESS_ADMIN_TOKEN ?? "";
if (!base.startsWith("https://") || !token) {
  console.error("Set ACCESS_API_BASE_URL and ACCESS_ADMIN_TOKEN in your shell.");
  process.exit(2);
}

async function call(path, options = {}, printResponse = true) {
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
  if (printResponse) console.log(JSON.stringify(body, null, 2));
  return body;
}

switch (command) {
  case "create": {
    const count = Number.parseInt(args[0] ?? "1", 10);
    const note = args.slice(1).join(" ") || undefined;
    await call("/v1/admin/codes", { method: "POST", body: JSON.stringify({ count, note }) });
    break;
  }
  case "create-file": {
    const outputPath = resolve(args[0] ?? "");
    const count = Number.parseInt(args[1] ?? "1", 10);
    const note = args.slice(2).join(" ") || undefined;
    if (!args[0]) throw new Error("Usage: admin create-file <output-path> <count> [note]");
    if (!Number.isInteger(count) || count < 1 || count > 100) {
      throw new Error("Count must be between 1 and 100.");
    }
    if (existsSync(outputPath)) throw new Error(`Refusing to overwrite ${outputPath}`);
    accessSync(dirname(outputPath), constants.W_OK);
    const body = await call(
      "/v1/admin/codes",
      { method: "POST", body: JSON.stringify({ count, note }) },
      false,
    );
    if (!Array.isArray(body.codes) || body.codes.length !== count ||
        body.codes.some((entry) => typeof entry?.code !== "string")) {
      throw new Error("The service returned an invalid code batch.");
    }
    const document = [
      `DualScreen Launcher - ${count} one-time access codes`,
      `Generated: ${new Date().toISOString()}`,
      "Each code can be redeemed once. Keep this file private.",
      "",
      ...body.codes.map((entry) => entry.code),
      "",
    ].join("\n");
    writeFileSync(outputPath, document, { encoding: "utf8", flag: "wx", mode: 0o600 });
    console.log(JSON.stringify({ created: count, output: outputPath }, null, 2));
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
    console.error("Commands: create [count] [note], create-file <path> <count> [note], codes, entitlements, revoke-code, revoke-entitlement, revoke-installation");
    process.exit(2);
}
