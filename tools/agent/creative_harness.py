#!/usr/bin/env python3
"""Native smoke trials on an empty, disposable scene. Never replaces an open map.

Exercises reviewed palettes, recipes, strict support checks, fixed camera A/B,
selective undo and preservation of the live camera. Not an aesthetic benchmark.
"""
import argparse
import json
import math
from pathlib import Path
import time
if __package__:
	from .service import ArianeService
else:
	from service import ArianeService
if __package__:
	from .creative_catalog import CreativeCatalog
else:
	from creative_catalog import CreativeCatalog
if __package__:
	from .review_rig import ReviewRig
else:
	from review_rig import ReviewRig

CASES = [
    ('military','base militaire usee','outpost',{'guard':16095,'barrier':2060,'tower':3279}),
    ('beach','campement plage','camp',{'shelter':642,'seating':1646,'cooking_fire':1481}),
    ('industrial','stockage industriel rouille','logistics',{'crate':2977,'barrel':1217,'utility':929}),
    ('rural','terrasse rurale bois','terrace',{'table':2111,'seat':1810}),
    ('garden','jardin parc fleuri','garden',{'seating':1368,'vegetation':630}),
    ('market','marche desert','market',{'counter':3861,'crate':964}),
]


def run(output: Path):
    output=output.resolve();output.mkdir(parents=True,exist_ok=True)
    s=ArianeService(timeout=60)
    status=s.engine('session_status')
    if status.get('active') or (status.get('logical_path') and not status['logical_path'].startswith('ariane\\creative-test-')) or status.get('live_instances'):
        raise ValueError('native harness requires a fresh empty engine; refuses to replace an open map')
    camera=s.engine('camera_context')['camera']
    env={k:v for k,v in s.environment().items() if k in ('hour','minute','weather_a','weather_b','blend')}
    token=str(time.time_ns())
    s.engine('scene',[f'ariane\\creative-test-{token}.ipl',output/f'trial-{token}.ipl'])
    s.engine('session_begin',[f'test-{token}'])
    catalogue=CreativeCatalog(s);report={'kind':'native-smoke-not-aesthetic-benchmark','cases':[],'terrain':[]}
    try:
        s.environment(hour=14,minute=0,weather_a=1,weather_b=1,blend=0)
        for name,brief,recipe,palette in CASES:
            target=output/name;target.mkdir(exist_ok=True)
            t=time.time()
            candidates=catalogue.palette(brief,['seating' if r=='seat' else r for r in palette],per_role=4,max_dimensions=[12,12,20])
            s._atomic_json(target/'palette.json',candidates)
            entry={'name':name,'brief':brief,'palette':palette,'missing_roles':candidates['missing_roles'],'variants':[]}
            for i in range(2):
                s.vibe.update_project({'brief':brief,'zones':[]})
                built=s.vibe.recipe(recipe,palette,key=f'{name}.{i}',x=1450,y=1600,heading=17 if i else 0,count=3,aisle=3+i,gap=.35+i*.3,seed=str(i))
                s.vibe.update_project({'zones':built['constraints']})
                plan=s.resolve_scene_patch(built['operations'])
                s._atomic_json(target/f'plan-{i}.json',plan)
                trial={'preflight':plan['preflight'],'object_count':len(built['operations'])}
                entry['variants'].append(trial)
                if not plan['preflight']['valid']:
                    continue
                receipt=s.apply_resolved_plan(plan['plan_id'])
                trial['validation']=s.validate_composition()
                # Every theme uses two fixed poses; the same rig is reused between its variants.
                rig=ReviewRig(s)
                if i==0:
                    extent=max(16, max(math.hypot(o['x']-1450,o['y']-1600) for o in built['operations'])*2+10)
                    views=[]
                    for label,pos in [('overview',[1450-extent*.8,1600-extent*.9,10+extent*.9]),('entry',[1450,1600-extent,15])]:
                        views.append(s.client.capture_at_pose(target/f'{label}.png',position=pos,target=[1450,1600,13],fov=58,label=label))
                    s._atomic_json(target/'source.json',{'views':views})
                    rig.create(str(target/'source.json'),str(target/'rig.json'))
                rig.capture(str(target/'rig.json'),str(target/f'variant-{i}'))
                s.checkpoint_save(f'creative-{token}-{name}-{i}')
                # Exercise real selective undo, checking the scratch layer is empty again.
                s.vibe.undo(receipt['edit_id'])
                trial['undo_empty']=len(s.enumerate_scene())==0
                if not trial['undo_empty']:raise RuntimeError('undo left test objects behind')
            if all(v['preflight']['valid'] for v in entry['variants']):
                rig.compare([str(target/f'variant-{i}'/'rig-capture.json') for i in range(2)],str(target/'comparison.png'))
            entry['seconds']=round(time.time()-t,2);report['cases'].append(entry)
            s._atomic_json(output/'report.json',report)
            print(name,[(v['preflight']['valid'],v.get('undo_empty')) for v in entry['variants']],flush=True)
        s.vibe.update_project({'zones':[]})
        for label,x,y,z in [('flat',1450,1600,10),('gentle-desert',-700,1900,-40),('steep-hill',-868,-1650,111),('shoreline',400,-1900,0),('missing-ground',5000,5000,0)]:
            operations=[{'action':'place','key':f'terrain.{label}','model':16095,'x':x,'y':y,'z':z,'heading':37,'snap':True}]
            try:
                plan=s.resolve_scene_patch(operations)
                item={'site':label,'valid':plan['preflight']['valid'],'support_issues':plan['preflight']['support_issues']}
            except RuntimeError as error:
                if label != 'missing-ground' or 'no ground' not in str(error): raise
                item={'site':label,'valid':False,'expected_rejection':str(error)}
            report['terrain'].append(item)
            print('terrain',label,item['valid'],flush=True)
        after=s.engine('camera_context')['camera']
        report['camera_preserved']=all(camera[k]==after[k] for k in ('position','target','fov'))
        report['remaining_test_objects']=len(s.enumerate_scene())
    finally:
        s.vibe.update_project({'zones':[]})
        s.environment(**env)
        s.engine('session_rollback')
        s._atomic_json(output/'report.json',report)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    print(json.dumps(run(parser.parse_args().output),indent=2))
