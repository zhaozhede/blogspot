#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 static/landing-quotes.json 生成 src/landing_quotes_embed.h（C 结构体数组，供 handle_landing 服务端选句）。"""
import json
import os
import sys

def c_escape(s):
    if s is None:
        return ""
    return (s.replace("\\", "\\\\")
             .replace('"', '\\"')
             .replace("\n", "\\n")
             .replace("\r", "\\r"))

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    json_path = os.path.join(root, "static", "landing-quotes.json")
    out_path = os.path.join(root, "src", "landing_quotes_embed.h")
    if not os.path.isfile(json_path):
        print("missing:", json_path, file=sys.stderr)
        sys.exit(1)
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    lines = [
        "/* 由 tools/gen_landing_quotes_h.py 根据 static/landing-quotes.json 生成，勿手改 */",
        "#ifndef LANDING_QUOTES_EMBED_H",
        "#define LANDING_QUOTES_EMBED_H",
        "typedef struct { const char *q; const char *f; const char *e; } landing_quote_t;",
        "static const landing_quote_t LANDING_QUOTES[] = {",
    ]
    for item in data:
        q = c_escape(item.get("q", ""))
        f = c_escape(item.get("f", ""))
        e = c_escape(item.get("e", ""))
        lines.append('  { "%s", "%s", "%s" },' % (q, f, e))
    lines.append("};")
    lines.append("#define LANDING_QUOTES_COUNT (int)(sizeof(LANDING_QUOTES)/sizeof(LANDING_QUOTES[0]))")
    lines.append("#endif")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", out_path, "entries", len(data))

if __name__ == "__main__":
    main()
