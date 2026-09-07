"""Local, explainable concept retrieval. No claim of learned image embeddings.

Reviewed passports outrank names/tags; unreliable external descriptions are opt-in.
A role is a hard eligibility filter, style a soft preference, exclusions are hard.
"""
from __future__ import annotations
import json
import math
import re
import unicodedata
from pathlib import Path
from PIL import Image, ImageDraw
if __package__:
	from .asset_profiles import bundled_annotations
else:
	from asset_profiles import bundled_annotations

CONCEPTS = {
    'guard': 'guard guardhouse guérite guerite guardbox',
    'tower': 'tower mirador watchtower lookout',
    'military': 'military militaire armee army dayz armybase area51 a51 checkpoint barracks sandbag',
    'weathered': 'weathered use usee vieux vieille old rusty rust rouille rouillee abandoned abandonne broken wreck dirty',
    'industrial': 'industrial industriel industrielle factory usine warehouse cargo dock portuaire chantier',
    'rural': 'rural rurale ferme farm barn grange country campagne ranch',
    'beach': 'beach plage parasol surf seaside tropical',
    'desert': 'desert aride cactus cacti western',
    'urban': 'urban urbain urbaine ville street rue ruelle alley',
    'garden': 'garden jardin park parc vegetation flower plante',
    'wood': 'wood wooden bois timber',
    'metal': 'metal metallic acier steel iron fer',
    'concrete': 'concrete beton cement',
    'seating': 'seating seat seats siege sieges chair chaise chaises bench banc bancs stool fauteuil sofa',
    'table': 'table tables desk bureau picnic parktable',
    'counter': 'counter comptoir marketstall etal',
    'container': 'container conteneur containers stockage storage',
    'crate': 'crate crates caisse caisses box pallet palette',
    'barrel': 'barrel barrels drum drums bidon fut tonneau',
    'fence': 'fence fencing cloture grillage barbed barbwire',
    'barrier': 'barrier barricade barricades barrage roadblock sandbag sacs de sable',
    'shelter': 'shelter abri canopy tent tente parasol gazebo awning',
    'lighting': 'lighting light lamp lampe eclairage floodlight projecteur lantern',
    'vegetation': 'vegetation tree arbre arbres bush buisson plante plant flower fleurs palm',
    'utility': 'utility generator generateur pump pompe tank reservoir',
    'signage': 'signage sign panneau pancarte billboard enseigne',
    'waste': 'waste trash rubbish dumpster garbage poubelle dechets',
    'cooking_fire': 'barbecue bbq barbeque campfire foyer',
    'building': 'building batiment house maison shed cabane warehouse garage motel',
}


def words(value):
    value = unicodedata.normalize('NFKD', str(value)).encode('ascii', 'ignore').decode().lower()
    return set(re.findall(r'[a-z]+|[0-9]+', value.replace('_', ' ')))

# Phrase aliases are explicit; do not expand individual words of 'sacs de sable'.
ALIASES = {key: words(value) for key, value in CONCEPTS.items()}
ALIASES['barrier'] -= {'sacs', 'de', 'sable'}


def concepts(value):
    tokens = words(value)
    result = {key for key, aliases in ALIASES.items() if tokens & aliases or key in tokens
              or any(len(alias) >= 5 and (token.startswith(alias) or token.endswith(alias))
                     for alias in aliases for token in tokens)}
    if {'sacs', 'sable'} <= tokens:
        result.add('barrier')
    if str(value) in ALIASES: result.add(str(value))
    return tokens | result


class CreativeCatalog:
    def __init__(self, service):
        self.s = service
        self._snapshot = None

    def _rows(self):
        if self._snapshot is not None:
            return self._snapshot
        annotations = {int(k):v for k,v in bundled_annotations().items()}
        for path in self.s.profiles.root.glob('*.json'):
            if path.stem.isdigit():
                annotations[int(path.stem)] = json.loads(path.read_text())
        rows = []
        for asset in self.s.assets.catalog():
            annotation = annotations.get(int(asset['id']))
            features = (annotation or {}).get('features', {})
            identity = ' '.join(str(asset.get(k) or '') for k in ('name','description','tags','role_tags','style_tags','category'))
            rows.append((asset, annotation, features, concepts(identity)))
        self._snapshot = rows
        return rows

    def search(self, query: str, role: str | None = None, styles: list[str] | None = None,
               avoid: list[str] | None = None, limit: int = 24,
               max_dimensions: list[float] | None = None, include_external: bool = False):
        if not query.strip() and not role:
            raise ValueError('provide a query or role')
        if not 1 <= limit <= 100:
            raise ValueError('limit must be 1-100')
        if max_dimensions is not None and (len(max_dimensions) != 3 or any(
                not math.isfinite(v) or v <= 0 for v in max_dimensions)):
            raise ValueError('max_dimensions must be three finite positive xyz sizes')
        query_terms = concepts(query)
        style_terms = set().union(*(concepts(s) for s in styles or []))
        excluded = [concepts(term) for term in avoid or [] if term.strip()]
        role_terms = concepts(role or '')
        rows = []
        for asset, annotation, features, identity in self._rows():
            observed = concepts(json.dumps(features, ensure_ascii=False)) if features else set()
            eligible = identity | observed
            role_evidence = (set().union(*(concepts(r) for r in features['roles'])) if 'roles' in features
                             else concepts(str(asset.get('name','')) + ' ' + str(asset.get('role_tags',''))))
            # For known roles use its concept, not a loose OR over the entire brief.
            required = {r for r in ALIASES if r in role_terms} or role_terms
            if required and not required <= role_evidence:
                continue
            if any(term & eligible for term in excluded):
                continue
            dims = [asset.get(k) for k in ('width','depth','height')]
            if max_dimensions and any(v is None or not math.isfinite(float(v)) or float(v) <= 0 or float(v) > cap
                                      for v, cap in zip(dims, max_dimensions)):
                continue
            external = concepts(asset.get('semantic_description') or '') if include_external else set()
            matches = query_terms & eligible
            if not matches and not role and not (query_terms & external):
                continue
            confidence = float((annotation or {}).get('confidence', 0))
            score = len(query_terms & identity) + 4 * confidence * len(query_terms & observed)
            score += 2 * len(style_terms & identity) + 4 * confidence * len(style_terms & observed)
            score += .15 * len(query_terms & external)
            rows.append({'asset_id': int(asset['id']), 'name': asset['name'], 'dimensions': dims,
                         'family': asset.get('family_key'), 'score': round(score, 3),
                         'matched': sorted(matches), 'style_matches': sorted(style_terms & eligible),
                         'observed_features': features, 'annotation_source': (annotation or {}).get('source'),
                         'annotation_confidence': confidence, 'external_description_used': include_external,
                         'requires_visual_review': True})
        rows.sort(key=lambda r: (-r['score'], r['asset_id']))
        # Round-robin families within relevance bands to avoid 24 near-identical crates.
        selected, counts = [], {}
        while rows and len(selected) < limit:
            best = max(range(len(rows)), key=lambda i: (rows[i]['score'] - 2 * counts.get(rows[i]['family'] or rows[i]['name'], 0), -rows[i]['asset_id']))
            row = rows.pop(best)
            family = row['family'] or row['name']
            counts[family] = counts.get(family, 0) + 1
            selected.append(row)
        return {'method': 'multilingual-concepts-v1', 'assets': selected,
                'query_concepts': sorted(query_terms & ALIASES.keys()),
                'limitation': 'Local vocabulary and sourced annotations, not learned image semantics. Verify previews.'}

    def palette(self, brief: str, roles: list[str], styles: list[str] | None = None,
                avoid: list[str] | None = None, per_role: int = 4,
                max_dimensions: list[float] | None = None):
        if not 1 <= len(roles) <= 12 or len(set(roles)) != len(roles) or not 1 <= per_role <= 8:
            raise ValueError('provide 1-12 unique roles and 1-8 candidates per role')
        groups = {role: self.search(brief, role, styles, avoid, per_role, max_dimensions)['assets'] for role in roles}
        return {'brief': brief, 'roles': groups, 'missing_roles': [r for r, rows in groups.items() if not rows],
                'requires_visual_review': True, 'selection_policy': 'Hard role/size/exclusion filters; soft style ranking.'}

    def board(self, asset_ids: list[int], output_directory: str, size: int = 192):
        if not 1 <= len(asset_ids) <= 32 or not 96 <= size <= 512:
            raise ValueError('board accepts 1-32 assets, size 96-512')
        output = Path(output_directory).resolve()
        output.mkdir(parents=True, exist_ok=True)
        rows, errors = [], []
        sheet = Image.new('RGB', (size*4, (size+70)*math.ceil(len(asset_ids)/4)), '#20242a')
        draw = ImageDraw.Draw(sheet)
        for i, asset_id in enumerate(asset_ids):
            x, y = i % 4 * size, i // 4 * (size+70)
            try:
                asset = self.s.profiles.get(asset_id)['asset']
                path = output / f'{int(asset_id)}.png'
                self.s.render_asset_views(asset_id, path, size=size)
                with Image.open(path) as original:
                    thumb = original.convert('RGB')
                    thumb.thumbnail((size, size))
                    sheet.paste(thumb, (x, y))
                label = f"{asset_id}: {asset['name']}"
                draw.text((x+4, y+size+2), label[:30], fill='white')
                draw.text((x+4, y+size+22), ' x '.join(str(asset.get(k) or '?') for k in ('width','depth','height'))[:32], fill='#bfc8d5')
                rows.append({'asset_id':asset_id,'path':str(path),'name':asset['name']})
            except (OSError, ValueError, RuntimeError) as error:
                errors.append({'asset_id':asset_id,'error':str(error)})
                draw.text((x+4,y+size), f'{asset_id}: preview failed', fill='#ff9999')
        sheet.save(output/'board.png')
        report = {'path':str(output/'board.png'),'assets':rows,'errors':errors,'requires_visual_review':True}
        self.s._atomic_json(output/'board.json', report)
        return report
