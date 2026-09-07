"""Reproducible view rigs for visual A/B review; never moves the user's camera."""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw


class ReviewRig:
    def __init__(self, service): self.s = service

    def create(self, capture_manifest: str, output_path: str):
        manifest = json.loads(Path(capture_manifest).read_text())
        views = []
        for i, view in enumerate(manifest['views']):
            camera = view.get('actual_pose') or view.get('camera') or view.get('pose')
            if not camera:
                raise ValueError('capture has no camera provenance')
            views.append({'label': f'view-{i}', **{k:camera[k] for k in ('position','target','fov')}, 'up':camera.get('up_reference', camera['up'])})
        if not 1 <= len(views) <= 8: raise ValueError('rig requires 1-8 views')
        rig = {'schema':1,'views':views}
        rig['rig_id'] = hashlib.sha256(json.dumps(views,sort_keys=True).encode()).hexdigest()[:20]
        self.s._atomic_json(Path(output_path),rig)
        return rig

    def capture(self, rig_path: str, output_directory: str):
        rig = json.loads(Path(rig_path).read_text())
        if not 1 <= len(rig['views']) <= 8: raise ValueError('rig requires 1-8 views')
        expected = hashlib.sha256(json.dumps(rig['views'],sort_keys=True).encode()).hexdigest()[:20]
        if rig['rig_id'] != expected: raise ValueError('rig changed; recreate it before comparison')
        before = self.s.engine('session_status')
        environment = {k:v for k,v in self.s.environment().items() if k in ('hour','minute','weather_a','weather_b','blend')}
        output = Path(output_directory).resolve()
        views = []
        for i, pose in enumerate(rig['views']):
            views.append(self.s.client.capture_at_pose(output/f'view-{i}.png', **pose))
        after = self.s.engine('session_status')
        if before['scene_revision'] != after['scene_revision']:
            raise ValueError('scene changed during rig capture')
        report = {'rig_id':rig['rig_id'],'scene_revision':after['scene_revision'],
                  'scene':before.get('logical_path'), 'environment':environment,'views':views}
        self.s._atomic_json(output/'rig-capture.json',report)
        return report

    def compare(self, capture_paths: list[str], output_path: str):
        if not 2 <= len(capture_paths) <= 3: raise ValueError('compare 2-3 captures')
        reports = [json.loads(Path(p).read_text()) for p in capture_paths]
        first = reports[0]
        for report in reports[1:]:
            if any(report[k] != first[k] for k in ('rig_id','environment','scene')) or len(report['views']) != len(first['views']):
                raise ValueError('comparison requires identical rig, environment and scene')
        sheet=Image.new('RGB',(480*len(reports), (290)*len(first['views'])),'#20242a')
        draw=ImageDraw.Draw(sheet)
        for col,report in enumerate(reports):
            for row,view in enumerate(report['views']):
                with Image.open(view['path']) as im:
                    thumb=im.convert('RGB'); thumb.thumbnail((480,260))
                    sheet.paste(thumb,(col*480,row*290))
                draw.text((col*480+8,row*290+264),f"Variant {col+1} / view {row+1} / revision {report['scene_revision']}",fill='white')
        target=Path(output_path).resolve(); target.parent.mkdir(parents=True,exist_ok=True); sheet.save(target)
        return {'path':str(target),'rig_id':first['rig_id'],'visual_review':'pending','sources':capture_paths}
