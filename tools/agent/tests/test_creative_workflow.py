"""Adversarial retrieval, rotated placements, reusable compositions and review rigs."""
import json
import math
from pathlib import Path
import random
import sys
import tempfile
import unittest
from unittest.mock import Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from creative_catalog import CreativeCatalog
from scene_recipes import RECIPES, build_recipe
from spatial import relative_offset, facing_heading, footprint, rectangle, polygons_overlap
from review_rig import ReviewRig


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.s=Mock();self.s.profiles.root=Path(self.tmp.name)
        self.s.assets.catalog.return_value=[
            dict(id=1,name='wooden_chair',role_tags='seating',style_tags='rural',width=1,depth=1,height=1),
            dict(id=2,name='steel_chair',role_tags='seating',style_tags='industrial',width=1,depth=1,height=1),
            dict(id=3,name='container_open',role_tags='container',width=3,depth=7,height=3,
                 semantic_description='wooden chair in a garden'),
            dict(id=4,name='giant_chair',role_tags='seating',width=30,depth=30,height=30),
        ]
        self.c=CreativeCatalog(self.s)

    def test_french_roles_and_hard_exclusion(self):
        result=self.c.search('une chaise en bois',role='siege',avoid=['metal'],max_dimensions=[2,2,2])
        self.assertEqual([1],[a['asset_id'] for a in result['assets']])

    def test_untrusted_external_description_cannot_fill_role(self):
        self.assertNotIn(3,[a['asset_id'] for a in self.c.search('chaise',role='seating')['assets']])
        self.assertEqual([],self.c.search('garden')['assets'])

    def test_visual_annotation_outranks_heuristic(self):
        (self.s.profiles.root/'2.json').write_text(json.dumps({'source':'native-preview.png','confidence':.9,
            'features':{'roles':['seating'],'styles':['rural'],'materials':['wood']}}))
        result=self.c.search('wooden chair',role='seating',styles=['rural'])
        self.assertEqual(2,result['assets'][0]['asset_id'])
        self.assertEqual('native-preview.png',result['assets'][0]['annotation_source'])

    def test_empty_palette_role_is_explicit(self):
        result=self.c.palette('plage', ['seating','shelter'])
        self.assertEqual(['shelter'],result['missing_roles'])
        self.assertEqual([],result['roles']['shelter'])

    def test_invalid_dimensions_and_role_counts(self):
        for sizes in ([1,2],[1,float('nan'),2],[-1,2,2]):
            with self.assertRaises(ValueError):self.c.search('chair',max_dimensions=sizes)
        with self.assertRaises(ValueError):self.c.palette('x',['seat','seat'])


class GeometryTests(unittest.TestCase):
    def test_rotated_offcentre_child_keeps_exact_gap(self):
        p=[[-2,-1,0],[2,1,3]];q=[[0,-3,0],[1,3,1]]
        x,y,z=relative_offset(p,q,0,90,'in_front',.3)
        parent=footprint({'position':[0,0,0],'rotation':[0,0,0]},p)
        child=footprint({'position':[x,y,z],'rotation':[0,0,90]},q)
        self.assertAlmostEqual(.3,min(v[1] for v in child)-max(v[1] for v in parent))
        self.assertFalse(polygons_overlap(parent,child))

    def test_functional_front_points_left_at_90(self):
        x,y,z=relative_offset([[-1,-1,0],[1,1,2]],[[-.5,-.5,0],[.5,.5,1]],0,0,'in_front',.2,90)
        self.assertAlmostEqual(-1.7,x);self.assertAlmostEqual(0,y)
        self.assertAlmostEqual(270,facing_heading(0,0,[1,0]))
        self.assertAlmostEqual(180,facing_heading(0,0,[1,0],90))

    def test_relations_random_headings_have_no_overlap(self):
        rng=random.Random(73)
        for _ in range(120):
            p=[[-2,-1,0],[3,2,4]];q=[[-.2,-2,0],[1,3,1]]
            ph,ch,front=[rng.uniform(-180,180) for _ in range(3)]
            for relation in ('in_front','behind','left','right'):
                x,y,z=relative_offset(p,q,ph,ch,relation,.2,front)
                c,s=math.cos(math.radians(ph)),math.sin(math.radians(ph))
                self.assertFalse(polygons_overlap(footprint({'position':[0,0,0],'rotation':[0,0,ph]},p),
                    footprint({'position':[x*c-y*s,x*s+y*c,z],'rotation':[0,0,ch]},q)))

    def test_all_recipes_keep_aisles_across_dimensions_and_rotations(self):
        rng=random.Random(91)
        for name,meta in RECIPES.items():
            if name=='fence':continue
            for heading in (0,37,90,173):
                roles=meta['roles'];palette={r:i+1 for i,r in enumerate(roles)}
                dims={r:[rng.uniform(.4,5),rng.uniform(.4,4),1] for r in roles}
                fronts={r:90 for r in roles}
                result=build_recipe(name,palette,dims,key=name,x=27,y=-13,heading=heading,count=3,front_headings=fronts)
                zone=result['constraints'][0]
                aisle=rectangle(zone['x'],zone['y'],zone['width'],zone['depth'],zone['heading'])
                polys=[]
                for op in result['operations']:
                    role=roles[op['model']-1];w,d,_=dims[role]
                    poly=rectangle(op['x'],op['y'],w,d,op['heading'])
                    self.assertFalse(polygons_overlap(poly,aisle),(name,heading,role,'aisle'))
                    self.assertTrue(all(not polygons_overlap(poly,other) for other in polys),(name,heading,role,'overlap'))
                    polys.append(poly)

    def test_rotated_market_preserves_margin_after_native_float_rounding(self):
        import struct
        f32=lambda v:struct.unpack('f',struct.pack('f',v))[0]
        dims={'counter':[3.1286,2.823,2.355], 'crate':[1.3281,1.3485,.9358]}
        result=build_recipe('market',{'counter':1,'crate':2},dims,key='market',x=1450,y=1600,heading=17,count=3,aisle=4,gap=0)
        zone=result['constraints'][0]
        aisle=rectangle(zone['x'],zone['y'],zone['width'],zone['depth'],zone['heading'])
        for op in result['operations']:
            w,d,_=dims['counter' if op['model']==1 else 'crate']
            poly=rectangle(f32(op['x']),f32(op['y']),w,d,f32(op['heading']))
            self.assertFalse(polygons_overlap(poly,aisle))


class RigTests(unittest.TestCase):
    def test_actual_pose_provenance_and_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);s=Mock()
            s._atomic_json.side_effect=lambda p,v:Path(p).write_text(json.dumps(v))
            source=root/'source.json';rig_path=root/'rig.json'
            source.write_text(json.dumps({'views':[{'actual_pose':{'position':[0,0,10],'target':[0,0,0],
                'up':[1,0,0],'up_reference':[0,1,0],'fov':60}}]}))
            rig=ReviewRig(s);result=rig.create(str(source),str(rig_path))
            self.assertEqual([0,1,0],result['views'][0]['up'])
            result['views'][0]['fov']=90;rig_path.write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError,'changed'):rig.capture(str(rig_path),str(root/'out'))
            s.engine.assert_not_called()

    def test_comparison_rejects_different_weather(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths=[]
            for i in range(2):
                p=Path(tmp)/f'{i}.json';p.write_text(json.dumps({'rig_id':'r','scene':'s','environment':{'hour':i},'views':[]}));paths.append(str(p))
            with self.assertRaisesRegex(ValueError,'identical'):ReviewRig(Mock()).compare(paths,str(Path(tmp)/'out.png'))


# Exercise the wire protocol and service preflight, beyond pure layout math.
import test_vibe
from service import ArianeService
from unittest.mock import patch, PropertyMock

class PlacementIntegrationTests(unittest.TestCase):
    setUp=test_vibe.VibeTests.setUp
    tearDown=test_vibe.VibeTests.tearDown
    place=test_vibe.VibeTests.place

    def test_measured_bounds_anchors_align_after_rotation(self):
        self.place('parent',10)
        self.service.apply_scene_patch([{'action':'transform','key':'parent','x':10,'y':20,'z':0,'heading':90,'snap':False}])
        index=Mock();index.inspect.return_value={'id':1000}
        with patch.object(ArianeService,'assets',new_callable=PropertyMock,return_value=index):
            plan=self.service.resolve_scene_patch([{'action':'place_relative','key':'child','model':1001,
                'relative_to':'parent','parent_anchor':'bounds.top','child_anchor':'bounds.base',
                'supported_by':'parent','snap':False}])
        op=plan['operations'][0]
        self.assertEqual([10,20,1],[op[k] for k in ('x','y','z')])
        self.assertTrue(plan['preflight']['valid'])

    def test_facing_uses_observed_front_and_rejects_unknown_front(self):
        profiles=Mock();profiles.get.return_value={'annotation':None}
        op={'action':'place','key':'facing','model':1000,'x':0,'y':0,'z':0,'face_towards':[5,0],'snap':False}
        with patch.object(ArianeService,'profiles',new_callable=PropertyMock,return_value=profiles):
            with self.assertRaisesRegex(RuntimeError,'front unknown'):self.service.resolve_scene_patch([op])
            profiles.get.return_value={'annotation':{'features':{'front_heading':90}}}
            plan=self.service.resolve_scene_patch([op])
            self.assertEqual(180,plan['operations'][0]['heading'])
        self.assertEqual([],self.service.enumerate_scene())

    def test_tilted_parent_rejected_before_mutation(self):
        self.place('parent')
        self.service.apply_scene_patch([{'action':'transform3d','key':'parent','x':0,'y':0,'z':0,'heading':0,'pitch':20,'roll':0,'snap':False}])
        with self.assertRaisesRegex(RuntimeError,'tilted'):
            self.service.resolve_scene_patch([{'action':'place_relative','key':'child','model':1001,'relative_to':'parent','relation':'on_top','snap':False}])
        self.assertEqual(1,len(self.service.enumerate_scene()))

    def test_variant_generation_keeps_scene_and_project_unchanged(self):
        profiles=Mock();profiles.get.return_value={'asset':{'width':1,'depth':1,'height':1},'annotation':None}
        project=self.service.vibe.project();status=self.service.engine('session_status')
        with patch('vibe.AssetProfiles',return_value=profiles):
            result=self.service.vibe.recipe_variants('terrace',{'table':1000,'seat':1001},{'key':'t','x':10,'y':20})
        self.assertEqual(3,len(result['variants']))
        self.assertEqual([3,3.75,4.5],[r['constraints'][0]['width'] for r in result['variants']])
        self.assertEqual(project,self.service.vibe.project())
        self.assertEqual(status['scene_revision'],self.service.engine('session_status')['scene_revision'])

if __name__=="__main__": unittest.main()
