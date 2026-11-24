"""Print compact schemas. usage: schema.py <toolset> [tool ...]"""
import json,subprocess,sys,os
sys.stdout.reconfigure(encoding="utf-8")
o=subprocess.run([sys.executable,os.path.join(os.path.dirname(__file__),"mcp_call.py"),"describe_toolset",json.dumps({"toolset_name":sys.argv[1]})],capture_output=True,encoding="utf-8").stdout
d=json.loads(json.loads(o)["result"]["content"][0]["text"])
for t in d.get("tools",[]):
    n=t["name"].split(".")[-1]
    if len(sys.argv)<3 or n in sys.argv[2:]:
        print("##",n,":",t.get("description","")[:350].replace("\n"," "));print("  ",json.dumps(t.get("inputSchema",{}),ensure_ascii=False)[:1200])
