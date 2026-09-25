// MCP client written with the official MCP TypeScript SDK. It connects over Streamable
// HTTP to the C++ server (cpp_server.cpp, SocketsHpp MCPServer), lists its tools and
// calls them. Exits non-zero on any failure, so it doubles as an interop check.
//
//   npx tsx client.ts [url]        (default http://127.0.0.1:3000/mcp)

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";

const url = new URL(process.argv[2] ?? process.env.MCP_URL ?? "http://127.0.0.1:3000/mcp");

function textOf(result: Awaited<ReturnType<Client["callTool"]>>): string {
  const content = (result.content ?? []) as Array<{ type: string; text?: string }>;
  return content.filter((c) => c.type === "text").map((c) => c.text).join("\n");
}

async function main(): Promise<void> {
  const client = new Client({ name: "ts-sdk-client", version: "1.0.0" });
  const transport = new StreamableHTTPClientTransport(url);
  await client.connect(transport);

  const server = client.getServerVersion();
  console.log(`[TS client] Connected to ${server?.name} ${server?.version} at ${url}`);
  console.log(`[TS client] Negotiated protocol version: ${transport.protocolVersion ?? "(none)"}`);

  const { tools } = await client.listTools();
  console.log(`[TS client] Tools: ${tools.map((t) => t.name).join(", ")}`);
  for (const name of ["get_weather", "greet"]) {
    if (!tools.some((t) => t.name === name)) throw new Error(`server does not offer tool '${name}'`);
  }

  const weather = await client.callTool({ name: "get_weather", arguments: {} });
  console.log(`[TS client] get_weather -> ${textOf(weather)}`);

  const greeting = await client.callTool({ name: "greet", arguments: { name: "TypeScript" } });
  const greetingText = textOf(greeting);
  console.log(`[TS client] greet -> ${greetingText}`);
  if (!greetingText.includes("TypeScript")) throw new Error(`unexpected greet result: ${greetingText}`);

  await transport.terminateSession();  // HTTP DELETE: ends the session on the server
  await client.close();
  console.log("[TS client] OK");
}

main().catch((err) => {
  console.error("[TS client] FAILED:", err);
  process.exit(1);
});
