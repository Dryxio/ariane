"""Synthetic catalogue: tests must never depend on a developer cache or GTA files."""
import json
from pathlib import Path
from asset_index import build_index

def make_catalogue(root):
    root = Path(root)
    data = root / "fixture catalogue" / "models" / "data"
    data.mkdir(parents=True)
    models = [{"id": i+1000, "name": name, "dff": name, "txd": "fixture", "category": "Objects", "source": "fixture.ide"}
              for i, name in enumerate(["bench_wood", "table_picnic", "bbq_grill", "trash_bin", "lamp_rural", "chair_desert", "barrel_rust", "cactus_small", "sign_motel", "fence_wood", "crate_wood", "stone_small"])]
    (data / "models.json").write_text(json.dumps({"models": models}))
    database = root / "test assets.sqlite3"
    build_index(data.parents[1], database)
    return database
