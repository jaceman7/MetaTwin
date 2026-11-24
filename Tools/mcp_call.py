"""Minimal Unreal MCP client. usage: mcp_call.py [tool] [json-args]  (no args: tools/list)"""
import json, sys, urllib.request

URL = "http://127.0.0.1:8000/mcp"
VERSION = "2025-06-18"


class Unreal:
    def __init__(self):
        self.sid, self.n = None, 0
        self.init = self.request("initialize", {"protocolVersion": VERSION, "capabilities": {},
                                                "clientInfo": {"name": "mcp-call", "version": "1"}})
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _post(self, msg):
        h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sid:
            h.update({"Mcp-Session-Id": self.sid, "MCP-Protocol-Version": VERSION})
        req = urllib.request.Request(URL, json.dumps(msg).encode(), h)
        with urllib.request.urlopen(req, timeout=180) as r:
            self.sid = r.headers.get("Mcp-Session-Id") or self.sid
            body = r.read().decode("utf-8")
        if not body.strip():
            return None
        if body.lstrip().startswith("{"):
            return json.loads(body)
        found = [json.loads(l[5:]) for l in body.splitlines() if l.startswith("data:") and l[5:].strip()]
        return next((m for m in found if m.get("id") == msg.get("id")), None)

    def request(self, method, params=None):
        self.n += 1
        return self._post({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params or {}})

    def call(self, toolset, tool, **args):
        """Call a toolset tool and return its returnValue; raise on any tool error."""
        out = self.request("tools/call", {"name": "call_tool", "arguments": {
            "toolset_name": toolset, "tool_name": tool, "arguments": args}})
        if not out or "error" in out or out["result"].get("isError"):
            raise RuntimeError(f"{tool}: {json.dumps(out, ensure_ascii=False)[:1500]}")
        content = out["result"]["content"]
        if len(content) == 1 and content[0]["type"] == "text":
            try:
                value = json.loads(content[0]["text"])
            except ValueError:
                return content[0]["text"]
            return value.get("returnValue", value) if isinstance(value, dict) else value
        return content


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    u = Unreal()
    if len(sys.argv) == 1:
        out = u.request("tools/list")
    else:
        out = u.request("tools/call", {"name": sys.argv[1], "arguments": json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}})
    print(json.dumps(out, ensure_ascii=False, indent=1))
