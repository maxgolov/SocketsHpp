import http from "http";
import dotenv from "dotenv";
dotenv.config({ path: "../.env" });

const CPP_SERVER_URL = "http://127.0.0.1:3000/mcp";

function callMcpMethod(method: string, params: any = {}): Promise<any> {
  const payload = JSON.stringify({
    jsonrpc: "2.0",
    id: Date.now(),
    method: method,
    params: params
  });

  return new Promise((resolve, reject) => {
    const url = new URL(CPP_SERVER_URL);
    const req = http.request({
      hostname: url.hostname,
      port: url.port || 80,
      path: url.pathname,
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Content-Length": Buffer.byteLength(payload)
      }
    }, res => {
      let data = "";
      res.on("data", chunk => { data += chunk; });
      res.on("end", () => {
        try {
          const response = JSON.parse(data);
          if (response.error) {
            reject(new Error(`JSON-RPC error: ${response.error.message}`));
          } else {
            resolve(response.result);
          }
        } catch (e) {
          reject(e);
        }
      });
    });
    req.on("error", reject);
    req.write(payload);
    req.end();
  });
}

(async () => {
  try {
    console.log(`[TS] Connecting to C++ MCP server at ${CPP_SERVER_URL}`);

    // Initialize - matches AI client handshake
    const initResponse = await callMcpMethod("initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "ts-client", version: "1.0.0" }
    });
    console.log(`[TS] Server:`, JSON.stringify(initResponse, null, 2));

    // List tools - matches AI SDK discovery
    const toolsResponse = await callMcpMethod("tools/list");
    console.log(`\n[TS] Tools:`, JSON.stringify(toolsResponse, null, 2));

    // Call tool - matches AI SDK execution
    const result = await callMcpMethod("tools/call", {
      name: "get_weather",
      arguments: {}
    });
    console.log(`\n[TS] Result:`, JSON.stringify(result, null, 2));
    
    if (result.content?.[0]?.text) {
      console.log(`[TS] ${result.content[0].text}`);
    }
  } catch (err: any) {
    console.error(`[TS] Error: ${err.message || err}`);
    process.exit(1);
  }
})();
