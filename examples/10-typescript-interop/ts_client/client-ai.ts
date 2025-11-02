// TypeScript AI client using Vercel AI SDK to call C++ MCP server
// Demonstrates real AI agent calling MCP tools exposed by C++ server

import { generateText, stepCountIs } from 'ai';
import { openai } from '@ai-sdk/openai';
import { z } from 'zod';
import dotenv from 'dotenv';
import http from 'http';

dotenv.config({ path: '../../../.env' });

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
    }, (res: any) => {
      let data = "";
      res.on("data", (chunk: any) => { data += chunk; });
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

async function getMcpTools() {
  const toolsResponse = await callMcpMethod("tools/list");
  console.log(`[TS AI] Available MCP tools:`, JSON.stringify(toolsResponse, null, 2));
  
  const tools: any = {};
  for (const tool of toolsResponse.tools || []) {
    // AI SDK v5 uses 'inputSchema' instead of 'parameters'
    // For tools with no parameters, use z.object({})
    tools[tool.name] = {
      description: tool.description || 'No description',
      inputSchema: z.object({}), // AI SDK v5 renamed 'parameters' to 'inputSchema'
      execute: async () => {  // No args for parameterless tools
        console.log(`[TS AI] Executing MCP tool: ${tool.name}`);
        const result = await callMcpMethod("tools/call", {
          name: tool.name,
          arguments: {}
        });
        
        if (result.content && Array.isArray(result.content)) {
          const textContent = result.content
            .filter((item: any) => item.type === 'text')
            .map((item: any) => item.text)
            .join('\n');
          return textContent;
        }
        return JSON.stringify(result);
      }
    };
  }
  return tools;
}

async function main() {
  try {
    console.log(`[TS AI] Connecting to C++ MCP server at ${CPP_SERVER_URL}`);

    const initResponse = await callMcpMethod("initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "ts-ai-client", version: "1.0.0" }
    });
    console.log(`[TS AI] C++ Server info:`, JSON.stringify(initResponse, null, 2));

    const tools = await getMcpTools();
    
    console.log(`\n[TS AI] Using OpenAI to generate response with MCP tools...\n`);
    
    const result = await generateText({
      model: openai('gpt-4o-mini'),
      prompt: 'What is the weather like? Please check and tell me.',
      tools: tools,
      stopWhen: stepCountIs(5),
    });

    console.log(`\n[TS AI] ========== AI RESPONSE ==========`);
    console.log(result.text);
    console.log(`[TS AI] ===================================\n`);

    if (result.steps && result.steps.length > 0) {
      console.log(`[TS AI] Tool calls made by AI:`);
      for (const step of result.steps) {
        if (step.toolCalls && step.toolCalls.length > 0) {
          for (const toolCall of step.toolCalls) {
            // In AI SDK v5, tool calls have 'input' property instead of 'args'
            const input = (toolCall as any).input || {};
            console.log(`  - ${toolCall.toolName}(${JSON.stringify(input)})`);
          }
        }
      }
    }

    console.log(`\n[TS AI] Test completed successfully!`);
  } catch (err: any) {
    console.error(`[TS AI] Error: ${err.message || err}`);
    if (err.stack) console.error(err.stack);
    process.exit(1);
  }
}

main();
