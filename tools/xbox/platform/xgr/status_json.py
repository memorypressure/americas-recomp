"""Write docs/xbox/xgr_status.json: for every xgameruntime interface, which of its methods have a real implementation in runtime/ (the rest log and fail).
usage: python -I status_json.py <table.json from idl_table.py> <out.json> [<runtime directory>]
Implemented = the method name appears as an override entry ({"Name", ...}) in the runtime sources. Names are public GDK API names. The runtime directory defaults to runtime/ next to this script.
gen_stub.py's PLAIN_EXPORTS (documented functions no interface maps) are listed under "Exports" when the runtime sources define them."""
import glob, json, os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
runtime = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "runtime")
plain = re.findall(r'"(X\w+)"', open(os.path.join(here, "gen_stub.py"), encoding="utf-8").read().split("PLAIN_EXPORTS", 1)[1].split("]", 1)[0])
impl = set()
srcs = []
for f in glob.glob(os.path.join(runtime, "*.cpp")):
    src = open(f, encoding="utf-8").read()
    srcs.append(src)
    impl.update(re.findall(r'\{"(X\w+)",\s*reinterpret', src))      # impl.cpp, impl_user.cpp
    impl.update(re.findall(r'GTX_OV\("(X\w+)"', src))               # xtask.cpp
combined = "\n".join(srcs)
defined = [n for n in plain if re.search(r'\b%s\b' % n, combined)]
t = json.load(open(sys.argv[1]))["interfaces"]
out = {}
for i in t:
    ms = [[m["name"], m["name"] in impl] for m in i["methods"] if not m.get("unnamed")]
    if ms:
        out[i["name"]] = ms
if defined:
    out["Exports"] = [[n, True] for n in defined]
json.dump(out, open(sys.argv[2], "w"), indent=0)
n = sum(len(v) for v in out.values()); d = sum(1 for v in out.values() for m in v if m[1])
print("%d interfaces, %d methods, %d implemented -> %s" % (len(out), n, d, sys.argv[2]))
