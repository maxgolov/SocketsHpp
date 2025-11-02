import http from "http";
import dotenv from "dotenv";
dotenv.config({ path: "../.env" });

const PORT = 3001;
const weatherKinds = ["sunny", "cloudy", "rainy", "stormy", "snowy", "windy"];

// MCP Protocol handler (JSON-RPC 2.0)
function handleMcp(reqBody: string): string {
  try {
    const req = JSON.parse(reqBody);
    const method = req.method;
    const params = req.params || {};
    const id = req.id;

    console.log(`[TS] Received MCP request: ${method}`);

    // Handle MCP methods
    if (method === "initialize") {
      const response = {
        jsonrpc: "2.0",
        result: {
          protocolVersion: "2024-11-05",
          capabilities: {
            tools: {}
          },
          serverInfo: {
            name: "ts-mcp-server",
            version: "1.0.0"
          }
        },
        id
      };
      return JSON.stringify(response);
    }
    
    if (method === "tools/list") {
      const response = {
        jsonrpc: "2.0",
        result: {
          tools: [
            {
              name: "get_weather",
              description: "Get random weather condition",
              inputSchema: {
                type: "object",
                properties: {}
              }
            },
            {
              name: "greet",
              description: "Greet a person by name",
              inputSchema: {
                type: "object",
                properties: {
                  name: {
                    type: "string",
                    description: "Name of the person to greet"
                  }
                },
                required: ["name"]
              }
            }
          ]
        },
        id
      };
      return JSON.stringify(response);
    }
    
    if (method === "tools/call") {
      const toolName = params.name;
      
      if (toolName === "get_weather") {
        const weather = weatherKinds[Math.floor(Math.random() * weatherKinds.length)];
        const response = {
          jsonrpc: "2.0",
          result: {
            content: [
              {
                type: "text",
                text: `Weather: ${weather}`
              }
            ]
          },
          id
        };
        return JSON.stringify(response);
      }
      
      if (toolName === "greet") {
        const name = params.arguments?.name || "stranger";
        const response = {
          jsonrpc: "2.0",
          result: {
            content: [
              {
                type: "text",
                text: `Hello, ${name}! Nice to meet you!`
              }
            ]
          },
          id
        };
        return JSON.stringify(response);
      }
      
      // Unknown tool
      return JSON.stringify({
        jsonrpc: "2.0",
        error: {
          code: -32602,
          message: `Unknown tool: ${toolName}`
        },
        id
      });
    }

    // Method not found
    return JSON.stringify({
      jsonrpc: "2.0",
      error: {
        code: -32601,
        message: `Method not found: ${method}`
      },
      id
    });
  } catch (e) {
    return JSON.stringify({
      jsonrpc: "2.0",
      error: {
        code: -32700,
        message: "Parse error"
      },
      id: null
    });
  }
}

const server = http.createServer((req, res) => {
  // Handle MCP endpoint
  if (req.method === "POST" && req.url === "/mcp") {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      const resp = handleMcp(body);
      res.setHeader("Content-Type", "application/json");
      res.setHeader("Access-Control-Allow-Origin", "*");
      res.end(resp);
    });
  } 
  // Handle OPTIONS for CORS
  else if (req.method === "OPTIONS" && req.url === "/mcp") {
    res.setHeader("Access-Control-Allow-Origin", "*");
    res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.setHeader("Access-Control-Allow-Headers", "Content-Type");
    res.writeHead(204);
    res.end();
  }
  else {
    res.writeHead(404);
    res.end();
  }
});

server.listen(PORT, () => {
  console.log(`[TS] MCP server listening at http://localhost:${PORT}/mcp`);
});
