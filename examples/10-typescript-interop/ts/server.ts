// MCP server written with the official MCP TypeScript SDK, served over Streamable HTTP.
// The C++ client (cpp_client.cpp, SocketsHpp MCPClient) connects to it.
//
//   npx tsx server.ts [port]        (default port 3001, endpoint /mcp)

import { randomUUID } from "node:crypto";
import http from "node:http";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";

const port = Number(process.argv[2] ?? process.env.PORT ?? 3001);
const weatherKinds = ["sunny", "cloudy", "rainy", "stormy", "snowy", "windy"];

function createServer(): McpServer {
  const server = new McpServer({ name: "ts-mcp-server", version: "1.0.0" });

  server.registerTool(
    "get_weather",
    { description: "Get a random weather condition" },
    async () => {
      const weather = weatherKinds[Math.floor(Math.random() * weatherKinds.length)];
      return { content: [{ type: "text", text: `Weather: ${weather}` }] };
    },
  );

  server.registerTool(
    "greet",
    {
      description: "Greet a person by name",
      inputSchema: { name: z.string().describe("Name of the person to greet") },
    },
    async ({ name }) => ({ content: [{ type: "text", text: `Hello, ${name}! Nice to meet you!` }] }),
  );

  return server;
}

// One transport (and server instance) per MCP session, keyed by Mcp-Session-Id.
const transports = new Map<string, StreamableHTTPServerTransport>();

async function readJson(req: http.IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) chunks.push(chunk as Buffer);
  const text = Buffer.concat(chunks).toString("utf8");
  return text ? JSON.parse(text) : undefined;
}

const httpServer = http.createServer(async (req, res) => {
  if (!req.url?.startsWith("/mcp")) {
    res.writeHead(404).end();
    return;
  }
  try {
    const sessionId = req.headers["mcp-session-id"] as string | undefined;
    const body = req.method === "POST" ? await readJson(req) : undefined;

    let transport = sessionId ? transports.get(sessionId) : undefined;
    if (!transport) {
      if (req.method !== "POST" || !isInitializeRequest(body)) {
        res.writeHead(400, { "Content-Type": "application/json" }).end(
          JSON.stringify({
            jsonrpc: "2.0",
            error: { code: -32000, message: "Bad Request: no valid session" },
            id: null,
          }),
        );
        return;
      }
      transport = new StreamableHTTPServerTransport({
        sessionIdGenerator: () => randomUUID(),
        onsessioninitialized: (id) => {
          transports.set(id, transport!);
        },
      });
      transport.onclose = () => {
        if (transport!.sessionId) transports.delete(transport!.sessionId);
      };
      await createServer().connect(transport);
    }
    await transport.handleRequest(req, res, body);
  } catch (err) {
    console.error("[TS server] request failed:", err);
    if (!res.headersSent) res.writeHead(500).end();
  }
});

httpServer.listen(port, "127.0.0.1", () => {
  console.log(`[TS server] MCP server (official SDK) listening at http://127.0.0.1:${port}/mcp`);
});

for (const signal of ["SIGINT", "SIGTERM"] as const) {
  process.on(signal, () => {
    httpServer.close();
    process.exit(0);
  });
}
