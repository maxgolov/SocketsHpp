import http from "http";
import dotenv from "dotenv";
dotenv.config({ path: "../.env" });

const PORT = 3001;
const weatherKinds = ["sunny", "cloudy", "rainy", "stormy", "snowy", "windy"];

// Minimal ModelContext (JSON-RPC 2.0) handler
function handleRpc(reqBody: string): string {
  try {
    const req = JSON.parse(reqBody);
    if (req.method === "call_tool" && req.params?.tool === "get_weather") {
      const response = {
        jsonrpc: "2.0",
        result: { kind: weatherKinds[Math.floor(Math.random() * weatherKinds.length)] },
        id: req.id || null,
      };
      return JSON.stringify(response);
    }
    return JSON.stringify({ jsonrpc: "2.0", error: { code: -32601, message: "Not implemented" }, id: req.id || null });
  } catch (e) {
    return JSON.stringify({ jsonrpc: "2.0", error: { code: -32700, message: "Parse error" }, id: null });
  }
}

const server = http.createServer((req, res) => {
  if (req.method === "POST") {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      const resp = handleRpc(body);
      res.setHeader("Content-Type", "application/json");
      res.end(resp);
    });
  } else {
    res.writeHead(404);
    res.end();
  }
});

server.listen(PORT, () => {
  console.log(`[TS] MCP server listening at http://localhost:${PORT}/`);
});
