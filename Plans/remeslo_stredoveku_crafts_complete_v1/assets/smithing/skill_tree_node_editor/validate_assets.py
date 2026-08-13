#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parent
manifest=json.loads((ROOT/"integration/smithing_skill_tree_assets.json").read_text(encoding="utf-8"))
tree=json.loads((ROOT/"integration/smithing_skill_tree.json").read_text(encoding="utf-8"))
errors=[]

atlas=Image.open(ROOT/manifest["atlas"]["file"])
if atlas.size != tuple(manifest["atlas"]["size"]): errors.append(f"atlas size {atlas.size}")
if atlas.mode != "RGBA": errors.append(f"atlas mode {atlas.mode}")

for key,sprite in manifest["sprites"].items():
    path=ROOT/sprite["file"]
    if not path.exists(): errors.append(f"missing {key}: {path}"); continue
    image=Image.open(path)
    if image.mode != "RGBA": errors.append(f"non-RGBA {key}: {image.mode}")
    if list(image.size) != sprite["native_size"]: errors.append(f"size mismatch {key}")
    x,y,w,h=sprite["src"]
    if x<0 or y<0 or x+w>atlas.width or y+h>atlas.height: errors.append(f"atlas bounds {key}")

node_ids={n["id"] for n in tree["nodes"]}
pins={n["input_pin"] for n in tree["nodes"] if n["input_pin"]}|{n["output_pin"] for n in tree["nodes"] if n["output_pin"]}
if len(node_ids)!=len(tree["nodes"]): errors.append("duplicate node id")
link_ids={link["id"] for link in tree["links"]}
if len(link_ids)!=len(tree["links"]): errors.append("duplicate link id")
for link in tree["links"]:
    if link["start_pin"] not in pins or link["end_pin"] not in pins: errors.append(f"bad link pins {link['id']}")

for rel in ("README.md","integration/SmithingSkillTreeAtlas.generated.h","integration/SmithingSkillTreeNodeEditor.h","integration/SmithingSkillTreeNodeEditor.cpp"):
    if not (ROOT/rel).exists(): errors.append(f"missing deliverable {rel}")

for rel,size in (("preview/smithing_skill_tree_node_editor_mockup.png",(1920,1080)),("preview/smithing_skill_tree_assets_overview.png",(1600,1000))):
    image=Image.open(ROOT/rel)
    if image.size!=size: errors.append(f"preview size {rel}: {image.size}")

result={"ok":not errors,"sprite_count":len(manifest["sprites"]),"node_count":len(tree["nodes"]),"link_count":len(tree["links"]),"atlas_size":list(atlas.size),"errors":errors}
(ROOT/"integration/validation.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
print(json.dumps(result,ensure_ascii=False,indent=2))
raise SystemExit(0 if not errors else 1)
