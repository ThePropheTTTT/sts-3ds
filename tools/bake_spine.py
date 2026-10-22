#!/usr/bin/env python3
"""Bake a static frame of a Spine monster out of its atlas + skeleton.json.

The 3DS port has no Spine runtime, so Act 1 monsters (which only exist as Spine
skeletons) are flattened here: the setup pose is evaluated once, every slot is
composited in draw order, and the result becomes a normal sprite.

This is an approximation: region offsets inside the atlas are applied as Spine
2.x does (bottom-centre origin), bones are evaluated with translation, rotation
and scale, and animations are ignored.

    python tools/bake_spine.py --monster images/monsters/theBottom/slimeM
"""

from __future__ import annotations

import argparse
import io
import json
import math
import os
import zipfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JAR = os.environ.get("STS_JAR", "desktop-1.0.jar")
CANVAS = 512


def mat_mul(m, n):
    """m * n for 2D affine matrices (a, b, c, d, tx, ty)."""
    a1, b1, c1, d1, x1, y1 = m
    a2, b2, c2, d2, x2, y2 = n
    return (
        a1 * a2 + c1 * b2,
        b1 * a2 + d1 * b2,
        a1 * c2 + c1 * d2,
        b1 * c2 + d1 * d2,
        a1 * x2 + c1 * y2 + x1,
        b1 * x2 + d1 * y2 + y1,
    )


def mat_apply(m, x, y):
    a, b, c, d, tx, ty = m
    return (a * x + c * y + tx, b * x + d * y + ty)


def parse_atlas(text):
    """-> {region: (page, x, y, w, h, orig_w, orig_h, off_x, off_y, rotate)}"""
    regions = {}
    page = None
    name = None
    props = {}
    attrs = ("rotate", "xy", "size", "orig", "offset", "index")

    def flush():
        nonlocal name, props
        if name and page:
            w, h = props.get("w", 0), props.get("h", 0)
            regions[name] = (
                page,
                props.get("x", 0),
                props.get("y", 0),
                w,
                h,
                props.get("orig_w", w),
                props.get("orig_h", h),
                props.get("off_x", 0),
                props.get("off_y", 0),
                props.get("rotate", False),
            )
        name, props = None, {}

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            flush()
            continue
        if ":" in line:
            key, value = (part.strip() for part in line.split(":", 1))
            if key not in attrs:
                continue
            if key == "xy":
                props["x"], props["y"] = (int(v) for v in value.split(","))
            elif key == "size":
                props["w"], props["h"] = (int(v) for v in value.split(","))
            elif key == "orig":
                props["orig_w"], props["orig_h"] = (int(v) for v in value.split(","))
            elif key == "offset":
                props["off_x"], props["off_y"] = (int(v) for v in value.split(","))
            elif key == "rotate":
                props["rotate"] = value.lower() == "true"
            continue
        if line.endswith(".png") and page is None:
            page = line
            continue
        flush()
        name = line
    flush()
    return regions


def bake(monster_dir, page_images, out_size=96):
    with zipfile.ZipFile(JAR) as zf:
        names = zf.namelist()
        jsons = [n for n in names if n.startswith(monster_dir + "/") and n.endswith(".json")]
        atlases = [n for n in names if n.startswith(monster_dir + "/") and n.endswith(".atlas")]
        if not jsons or not atlases:
            return None
        jpath = next((n for n in jsons if n.endswith("skeleton.json")), jsons[0])
        apath = next((n for n in atlases if n.endswith("skeleton.atlas")), atlases[0])
        skeleton = json.loads(zf.read(jpath))
        atlas_text = zf.read(apath).decode("utf-8")
        regions = parse_atlas(atlas_text)

        atlas_dir = apath.rsplit("/", 1)[0]

        def page_image(page):
            key = atlas_dir + "/" + page
            if key not in page_images:
                page_images[key] = Image.open(
                    io.BytesIO(zf.read(key))
                ).convert("RGBA")
            return page_images[key]

        # Bone world transforms in the setup pose.  The game drives some limbs
        # through IK/path constraints, but evaluating them once without the
        # animations distorts the pose; the rest pose is what the sprites show.
        world = {}
        for bone in skeleton["bones"]:
            sx = bone.get("scaleX", 1.0)
            sy = bone.get("scaleY", 1.0)
            rot = math.radians(bone.get("rotation", 0.0))
            local = (
                math.cos(rot) * sx,
                math.sin(rot) * sx,
                -math.sin(rot) * sy,
                math.cos(rot) * sy,
                bone.get("x", 0.0),
                bone.get("y", 0.0),
            )
            parent = bone.get("parent")
            world[bone["name"]] = mat_mul(world[parent], local) if parent else local

        canvas = Image.new("RGBA", (CANVAS * 2, CANVAS * 2), (0, 0, 0, 0))
        cx = cy = CANVAS  # skeleton origin sits in the middle

        def attachment_matrix(entry):
            ax = entry.get("x", 0.0)
            ay = entry.get("y", 0.0)
            arot = math.radians(entry.get("rotation", 0.0))
            ascale_x = entry.get("scaleX", 1.0)
            ascale_y = entry.get("scaleY", 1.0)
            return (
                math.cos(arot) * ascale_x,
                math.sin(arot) * ascale_x,
                -math.sin(arot) * ascale_y,
                math.cos(arot) * ascale_y,
                ax,
                ay,
            )

        def region_for(entry, slot):
            attach = slot.get("attachment")
            for key in (entry.get("path"), attach):
                if not key:
                    continue
                hit = regions.get(key) or regions.get(key.split("/")[-1])
                if hit:
                    return hit
            return None

        def mesh_points(entry, nv, m):
            """Evaluate either flat (x,y) or [boneCount,(bone,x,y,w)*] vertices."""
            verts = entry.get("vertices") or []
            if len(verts) == nv * 2:
                return [mat_apply(m, verts[i * 2], verts[i * 2 + 1]) for i in range(nv)]
            bones = skeleton["bones"]
            out = []
            i = 0
            for _ in range(nv):
                count = int(verts[i])
                i += 1
                px = py = 0.0
                for _ in range(count):
                    bone_i = int(verts[i])
                    x, y, weight = verts[i + 1], verts[i + 2], verts[i + 3]
                    i += 4
                    wx, wy = mat_apply(world[bones[bone_i]["name"]], x, y)
                    px += wx * weight
                    py += wy * weight
                out.append((px, py))
            return out

        def render_mesh(entry, slot):
            region = region_for(entry, slot)
            uvs = entry.get("uvs") or []
            triangles = entry.get("triangles") or []
            nv = len(uvs) // 2
            if not region or not triangles or nv < 3:
                return None
            page, rx, ry, rw, rh, _ow, _oh, _ox, _oy, rotate = region
            # For rotated atlas regions the stored rect has swapped dimensions.
            # Mesh UVs are authored against that stored rect, so sample it as-is.
            tw, th = (rh, rw) if rotate else (rw, rh)
            tex = page_image(page).crop((rx, ry, rx + tw, ry + th))
            tex = np.asarray(tex, dtype=np.float32) / 255.0
            th, tw = tex.shape[:2]
            m = mat_mul(world[slot["bone"]], attachment_matrix(entry))
            points = mesh_points(entry, nv, m)
            screen = [(cx + x, cy - y) for x, y in points]
            xs = [p[0] for p in screen]
            ys = [p[1] for p in screen]
            bx = max(0, int(math.floor(min(xs))) - 1)
            by = max(0, int(math.floor(min(ys))) - 1)
            ex = min(canvas.width, int(math.ceil(max(xs))) + 1)
            ey = min(canvas.height, int(math.ceil(max(ys))) + 1)
            if ex <= bx or ey <= by:
                return None
            w, h = ex - bx, ey - by
            tile = np.zeros((h, w, 4), dtype=np.float32)
            for t in range(0, len(triangles) - 2, 3):
                idx = (triangles[t], triangles[t + 1], triangles[t + 2])
                (x0, y0), (x1, y1), (x2, y2) = [screen[i] for i in idx]
                (u0, v0), (u1, v1), (u2, v2) = [(uvs[i * 2], uvs[i * 2 + 1]) for i in idx]
                den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
                if abs(den) < 1e-9:
                    continue
                sg = 1.0 if den > 0 else -1.0
                X, Y = np.meshgrid(
                    np.arange(bx, ex) + 0.5, np.arange(by, ey) + 0.5
                )
                b0 = sg * ((y1 - y2) * (X - x2) + (x2 - x1) * (Y - y2)) / abs(den)
                b1 = sg * ((y2 - y0) * (X - x2) + (x0 - x2) * (Y - y2)) / abs(den)
                b2 = 1.0 - b0 - b1
                mask = (b0 >= -0.002) & (b1 >= -0.002) & (b2 >= -0.002)
                if not mask.any():
                    continue
                U = b0 * u0 + b1 * u1 + b2 * u2
                V = b0 * v0 + b1 * v1 + b2 * v2
                if rotate:
                    # UVs are authored in the unrotated frame; the packer
                    # stored the texture turned 90 degrees counter-clockwise.
                    sx = np.clip((V * tw).astype(np.int32), 0, tw - 1)
                    sy = np.clip(((1.0 - U) * th).astype(np.int32), 0, th - 1)
                else:
                    # Mesh UVs are top-left (libGDX TextureRegion) in the
                    # unrotated region; do not flip V here.
                    sx = np.clip((U * tw).astype(np.int32), 0, tw - 1)
                    sy = np.clip((V * th).astype(np.int32), 0, th - 1)
                src = tex[sy, sx]
                alpha = src[..., 3] * mask
                tile[..., :3] = src[..., :3] * alpha[..., None] + tile[..., :3] * (1 - alpha[..., None])
                tile[..., 3] = alpha + tile[..., 3] * (1 - alpha)
            out = Image.fromarray((np.clip(tile, 0.0, 1.0) * 255).astype(np.uint8), "RGBA")
            return out, (bx, by)

        skins = skeleton.get("skins") or {}
        skin = skins.get("default", {})
        for slot in skeleton["slots"]:
            bone_name = slot["bone"]
            attach_name = slot.get("attachment")
            if not attach_name:
                continue
            entry = skin.get(slot["name"], {}).get(attach_name)
            if not entry:
                continue
            if entry.get("type") == "mesh":
                made = render_mesh(entry, slot)
                if made:
                    tile, pos = made
                    canvas.alpha_composite(tile, pos)
                continue
            region = region_for(entry, slot)
            if not region:
                continue
            page, rx, ry, rw, rh, ow, oh, off_x, off_y, rotate = region
            if rotate:
                # The packer stored the region rotated 90 degrees CCW; the
                # stored rectangle is (h, w), and a clockwise turn restores it.
                img = page_image(page).crop((rx, ry, rx + rh, ry + rw)).rotate(-90, expand=True)
            else:
                img = page_image(page).crop((rx, ry, rx + rw, ry + rh))

            # Attachment transform inside the bone.
            m = mat_mul(world[bone_name], attachment_matrix(entry))

            # Spine region attachments pivot at the bottom centre of the region.
            width = entry.get("width", ow)
            height = entry.get("height", oh)
            scale_x = math.hypot(m[0], m[1]) or 1.0
            scale_y = math.hypot(m[2], m[3]) or 1.0
            angle = math.degrees(math.atan2(m[1], m[0]))

            piece = img
            if (rw, rh) != (width, height) and width > 0 and height > 0:
                piece = piece.resize((max(1, int(width)), max(1, int(height))), Image.LANCZOS)
            if off_x or off_y:
                # Crop origin offset: the trimmed region sits inside the original.
                fixed = Image.new("RGBA", (max(1, int(width)), max(1, int(height))), (0, 0, 0, 0))
                fixed.alpha_composite(piece, (off_x, off_y))
                piece = fixed
            if scale_x != 1.0 or scale_y != 1.0:
                piece = piece.resize(
                    (max(1, int(piece.width * scale_x)), max(1, int(piece.height * scale_y))),
                    Image.LANCZOS,
                )
            if abs(angle) > 0.01:
                piece = piece.rotate(angle, resample=Image.BICUBIC, expand=True)

            px, py = mat_apply(m, 0.0, 0.0)
            # bottom-centre anchor
            paste_x = int(cx + px - piece.width / 2)
            paste_y = int(cy - py - piece.height)
            canvas.alpha_composite(piece, (paste_x, paste_y)) if 0 <= paste_x < canvas.width and 0 <= paste_y < canvas.height else None

        bbox = canvas.getbbox()
        if not bbox:
            return None
        canvas = canvas.crop(bbox)
        scale = (out_size - 4) / max(canvas.width, canvas.height)
        return canvas.resize(
            (max(1, int(canvas.width * scale)), max(1, int(canvas.height * scale))), Image.LANCZOS
        )


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--monster", default="images/monsters/theBottom/slimeM")
    ap.add_argument("--out", default=os.path.join(HERE, ".tmp/spine_bake.png"))
    args = ap.parse_args(argv)

    pages = {}
    img = bake(args.monster, pages)
    if img is None:
        print("nothing rendered")
        return 1
    img.save(args.out)
    print(f"-> {args.out} ({img.width}x{img.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
