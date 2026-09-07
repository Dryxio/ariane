"""Dimension-aware, deterministic composition builders. All output is reviewable patches."""
from __future__ import annotations

import math
import random


RECIPES = {
    "camp": {"roles": ["shelter", "seating", "cooking_fire"], "description": "Camp modules around a protected pedestrian spine"},
    "logistics": {"roles": ["crate", "barrel", "utility"], "description": "Storage clusters alongside a loading lane"},
    "outpost": {"roles": ["guard", "barrier", "tower"], "description": "Guard, defenses and lookout along an access lane"},
    "garden": {"roles": ["seating", "vegetation"], "description": "Rest pockets and planting alongside a path"},
    "terrace": {"roles": ["table", "seat"], "description": "Tables, facing seats and clear central aisle"},
    "market": {"roles": ["counter", "crate"], "description": "Opposing stalls, stock behind, clear aisle"},
    "checkpoint": {"roles": ["barrier"], "description": "Roadside barriers preserving vehicle clearance"},
    "alley": {"roles": ["clutter"], "description": "Wall-side clusters preserving a central passage"},
    "fence": {"roles": ["fence"], "description": "Modules following a polyline with an optional gate gap"},
}


def build_recipe(name: str, palette: dict, dimensions: dict, *, key: str,
                 x: float, y: float, heading: float = 0, count: int = 3,
                 aisle: float = 3, gap: float = 0.25, seed: str = "0",
                 points: list[list[float]] | None = None, gate_width: float = 0,
                 front_headings: dict | None = None) -> dict:
    if name not in RECIPES:
        raise ValueError(f"unknown recipe: {name}")
    if type(count) is not int or not 1 <= count <= 40 or aisle < 1 or gap < 0 or gate_width < 0:
        raise ValueError("count must be 1-40, aisle >= 1, gap/gate_width >= 0")
    if not all(math.isfinite(float(v)) for v in (x, y, heading, aisle, gap, gate_width)):
        raise ValueError("recipe geometry must be finite")
    missing = set(RECIPES[name]["roles"]) - set(palette)
    if missing:
        raise ValueError(f"missing palette roles: {sorted(missing)}")
    front_headings = front_headings or {}
    rng = random.Random(str(seed))
    operations = []
    cosine, sine = math.cos(math.radians(heading)), math.sin(math.radians(heading))

    def size(role):
        values = dimensions[role]
        if len(values) < 2 or any(not math.isfinite(float(v)) or float(v) <= 0 for v in values[:2]):
            raise ValueError(f"missing physical dimensions for {role}")
        # Layout dimensions are measured in the functional-front frame.
        angle = math.radians(float(front_headings.get(role, 0)))
        c,s=abs(math.cos(angle)),abs(math.sin(angle))
        return float(values[0])*c+float(values[1])*s, float(values[0])*s+float(values[1])*c

    def place(role, u, v, facing=0):
        operations.append({"action": "place", "key": f"{key}.{len(operations):03d}",
                           "group": key, "model": int(palette[role]),
                           "x": x + u * cosine - v * sine, "y": y + u * sine + v * cosine,
                           "heading": (heading + facing - float(front_headings.get(role, 0))) % 360,
                           "snap": True})

    length = 0.0
    if name == "outpost":
        gr, br, tr = [math.hypot(*size(r))/2 for r in ("guard","barrier","tower")]
        # One guard and one landmark, rather than repeating whole towers.
        length = 2*max(gr,tr) + count*(2*br+gap) + 2
        place("guard", -(aisle/2+gr+gap), -length/2+gr, -90)
        place("tower", aisle/2+tr+gap, length/2-tr, 90)
        for row in range(count):
            v = -length/2 + (row+.5)*length/count
            place("barrier", -(aisle/2+br+gap), v + 2*gr+2*br+gap, 90)
            place("barrier", aisle/2+br+gap, v - 2*tr-2*br-gap, 90)
        # Compute the route in local coordinates even when the group is rotated.
        local_depths = [abs(-(op["x"]-x)*sine+(op["y"]-y)*cosine) for op in operations]
        length = 2*(max(local_depths)+max(gr,br,tr))
    elif name in {"camp", "logistics", "garden"}:
        roles = RECIPES[name]["roles"]
        radii = {role: math.hypot(*size(role))/2 for role in roles}
        # Small functional pockets, one per row, alternating sides of a clear route.
        depth = 2*sum(radii.values()) + gap*len(roles) + 1
        for row in range(count):
            side = -1 if row%2==0 else 1
            v = (row-(count-1)/2)*depth
            if name == "garden":
                sr,vr=radii["seating"],radii["vegetation"]
                place("seating",side*(aisle/2+sr+gap),v,side*90)
                place("vegetation",side*(aisle/2+2*sr+vr+2*gap),v)
            elif name == "logistics":
                cr,br,ur=radii["crate"],radii["barrel"],radii["utility"]
                place("crate",side*(aisle/2+max(cr,br)+gap),v)
                place("barrel",side*(aisle/2+max(cr,br)+gap),v+cr+br+gap)
                if row == count-1:
                    place("utility",side*(aisle/2+2*cr+ur+2*gap),v)
            else:
                hr,sr,fr=radii["shelter"],radii["seating"],radii["cooking_fire"]
                u=side*(aisle/2+max(hr,sr,fr)+gap)
                place("shelter",u,v)
                place("seating",u,v-hr-sr-gap,0)
                place("cooking_fire",u,v+hr+fr+gap,180)
        length=count*depth + 2*max(radii.values())
    elif name == "terrace":
        tw, td = size("table")
        sw, sd = size("seat")
        step = max(td, sw) + 2 * sd + 2 * gap + 1.2
        offset = aisle / 2 + tw / 2 + sd + gap
        for row in range(count):
            v = (row - (count - 1) / 2) * step
            for side in (-1, 1):
                u = side * offset
                place("table", u, v)
                place("seat", u, v - td / 2 - sd / 2 - gap, 0)
                place("seat", u, v + td / 2 + sd / 2 + gap, 180)
        length = count * step
    elif name == "market":
        cw, cd = size("counter")
        bw, bd = size("crate")
        step = max(cw, bw) + 1.2 + gap
        for row in range(count):
            v = (row - (count - 1) / 2) * step
            for side in (-1, 1):
                u = side * (aisle / 2 + cd / 2 + max(gap, .05))
                place("counter", u, v, side * 90)
                place("crate", side * (aisle / 2 + cd + bd / 2 + gap + max(gap, .05)), v, side * 90)
        length = count * step
    elif name == "checkpoint":
        bw, bd = size("barrier")
        for row in range(count):
            for side in (-1, 1):
                place("barrier", side * (aisle / 2 + bd / 2 + gap), row * (bw + gap), 90)
        length = count * (bw + gap)
    elif name == "alley":
        cw, cd = size("clutter")
        for row in range(count):
            for side in (-1, 1):
                place("clutter", side * (aisle / 2 + math.hypot(cw, cd) / 2 + gap),
                      row * (max(cw, cd) + gap + 0.8), rng.uniform(-8, 8))
        length = count * (max(cw, cd) + gap + 0.8)
    else:
        fw, _ = size("fence")
        if not points or len(points) < 2 or len(points) > 30:
            raise ValueError("fence requires 2-30 local xy points")
        for a, b in zip(points, points[1:]):
            if len(a) != 2 or len(b) != 2 or not all(math.isfinite(float(v)) for v in [*a, *b]):
                raise ValueError("fence points must be finite local xy")
            dx, dy = b[0] - a[0], b[1] - a[1]
            distance = math.hypot(dx, dy)
            if distance < fw:
                continue
            n = int((distance + gap) / (fw + gap))
            if n > 400:
                raise ValueError("fence segment exceeds 400 modules")
            for i in range(n):
                along = (distance - (n - 1) * (fw + gap)) / 2 + i * (fw + gap)
                if gate_width and abs(along - distance / 2) < (gate_width + fw) / 2:
                    continue
                place("fence", a[0] + dx * along / distance, a[1] + dy * along / distance,
                      math.degrees(math.atan2(dy, dx)))
    if not operations or len(operations) > 500:
        raise ValueError("recipe must produce 1-500 objects")
    return {"recipe": name, "seed": seed, "operations": operations,
            "constraints": [] if name == "fence" else [{
                "name": f"{key}.aisle", "role": "circulation", "shape": "rect",
                "x": x - (length / 2 * sine if name in {"alley", "checkpoint"} else 0),
                "y": y + (length / 2 * cosine if name in {"alley", "checkpoint"} else 0),
                "width": aisle, "depth": max(length, 1), "heading": heading, "severity": "error"}],
            "review_required": ["verify actual fronts", "terrain", "access", "visual composition"],
            "orientation_assumptions": {role: "annotated" if role in front_headings else "model +Y"
                                        for role in palette}}
