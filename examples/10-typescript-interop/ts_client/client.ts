import http from "http";
import dotenv from "dotenv";
dotenv.config({ path: "../.env" });

const CXX_SERVER_PORT = 3000;

function callMcpServer(): Promise<any> {
  const payload = JSON.stringify({
    jsonrpc: "2.0",
    id: 1,
    method: "call_tool",
    params: { tool: "get_weather", input: {} }
  });

  return new Promise((resolve, reject) => {
    const req = http.request({
      hostname: "127.0.0.1",
      port: CXX_SERVER_PORT,
      method: "POST",
      headers: { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(payload) }
    }, res => {
      let data = "";
      res.on("data", chunk => { data += chunk; });
      res.on("end", () => {
        try { resolve(JSON.parse(data)); } catch (e) { reject(e); }
      });
    });
    req.on("error", reject);
    req.write(payload);
    req.end();
  });
}

(async () => {
  try {
    const resp = await callMcpServer();
    console.log(`[TS] Response from C++ MCP server:`, resp);
  } catch (err) {
    console.error(`[TS] Failed to call C++ MCP server:`, err);
  }
})();
