"""Behavioural checks for persistent intent, selective editing and composition."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch, PropertyMock, Mock

AGENT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_DIR))
from catalogue_fixture import make_catalogue
from service import ArianeService
from simulation_harness import SimulatedArianeEngine
from scene_recipes import build_recipe
from spatial import rectangle, polygons_overlap
from asset_profiles import AssetProfiles


class VibeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.database = make_catalogue(self.root)
        self.engine = SimulatedArianeEngine(self.root / 'engine.sock', self.root / 'engine')
        self.engine.__enter__()
        self.engine.dispatch('layer_open', ['vibe'])
        self.service = ArianeService(self.root / 'engine.sock', state_dir=self.root / 'agent', database=self.database, discovery_dir=self.root / 'discovery', timeout=2)

    def tearDown(self):
        self.engine.__exit__(None, None, None)
        self.tmp.cleanup()

    def place(self, key, x=0, group=None):
        return self.service.apply_scene_patch([{'action':'place', 'key':key, 'model':1000,
                 'x':x, 'y':0, 'z':0, 'heading':0, 'snap':False, 'group':group}])

    def test_project_survives_new_service_and_checks_revision(self):
        self.service.vibe.update_project({'brief':'Un marché abandonné', 'styles':['weathered']}, 0)
        fresh = ArianeService(self.root / 'engine.sock', state_dir=self.root / 'agent', database=self.database, discovery_dir=self.root / 'discovery')
        self.assertEqual('Un marché abandonné', fresh.vibe.project()['brief'])
        with self.assertRaisesRegex(ValueError, 'revision'):
            fresh.vibe.update_project({'brief':'overwrite'}, 0)

    def test_locked_group_blocks_delete_transform_and_addition(self):
        self.place('a', group='terrace')
        self.service.vibe.update_project({'locked_groups':['terrace']})
        for operation in [{'action':'delete','key':'a'},
                          {'action':'transform','key':'a','x':9,'y':0,'z':0,'heading':0},
                          {'action':'place','key':'b','group':'terrace','model':1000,'x':9,'y':0,'heading':0}]:
            with self.assertRaisesRegex(ValueError, 'locked'):
                self.service.apply_scene_patch([operation])
        self.assertEqual(1, len(self.service.enumerate_scene()))

    def test_selective_undo_preserves_unrelated_later_object(self):
        first = self.place('a')
        self.place('b', x=10)
        self.service.vibe.undo(first['edit_id'])
        self.assertEqual(['b'], [o['object_key'] for o in self.service.enumerate_scene()])

    def test_selective_undo_refuses_later_transform(self):
        first = self.place('a')
        self.service.apply_scene_patch([{'action':'transform','key':'a','x':5,'y':0,'z':0,'heading':0,'snap':False}])
        with self.assertRaisesRegex(ValueError, 'later edit'):
            self.service.vibe.undo(first['edit_id'])

    def test_checkpoint_restores_groups_and_project(self):
        self.place('a', group='terrace')
        self.service.vibe.update_project({'brief':'First version'})
        self.service.checkpoint_save('first')
        self.place('b', 5)
        self.service.vibe.update_project({'brief':'Changed'})
        self.service.checkpoint_restore('first')
        self.assertEqual('First version', self.service.vibe.project()['brief'])
        self.assertEqual(['a'], self.service._load_composition()[0]['groups']['terrace'])
        self.service.transform_group('terrace', dx=2)
        self.assertEqual(2, self.service.enumerate_scene()[0]['position'][0])

    def test_locked_checkpoint_refuses_before_clearing_scene(self):
        self.place('a')
        self.service.checkpoint_save('first')
        self.service.vibe.update_project({'locked_keys':['a']})
        with self.assertRaisesRegex(ValueError, 'locked'):
            self.service.checkpoint_restore('first')
        self.assertEqual(1, len(self.service.enumerate_scene()))

    def test_project_route_blocks_plan_before_any_mutation(self):
        self.service.vibe.update_project({'zones':[{'name':'entry','shape':'rect','x':0,'y':0,
                                                   'width':4,'depth':4,'heading':45}]})
        plan = self.service.resolve_scene_patch([{'action':'place','key':'a','model':1000,
                    'x':0,'y':0,'z':0,'heading':0,'snap':False}])
        self.assertFalse(plan['preflight']['valid'])
        self.assertTrue(plan['preflight']['semantic_issues'])
        with self.assertRaisesRegex(RuntimeError, 'preflight'):
            self.service.apply_resolved_plan(plan['plan_id'])
        self.assertEqual([], self.service.enumerate_scene())

    def test_warning_zone_is_not_a_blocking_error(self):
        self.place('a')
        result = self.service.validate_semantic_zones([{'shape':'circle','x':0,'y':0,'radius':2,'severity':'warning'}])
        self.assertTrue(result['valid'])
        self.assertEqual(1, len(result['issues']))

    def test_transform3d_roundtrip_and_undo(self):
        self.place('a')
        result = self.service.apply_scene_patch([{'action':'transform3d','key':'a',
                    'x':2,'y':3,'z':1,'heading':45,'pitch':10,'roll':20,'snap':False}])
        self.assertEqual([10,20,45], self.service.enumerate_scene()[0]['rotation'])
        self.service.vibe.undo(result['edit_id'])
        self.assertEqual([0,0,0], self.service.enumerate_scene()[0]['rotation'])

    def test_replayed_plan_is_exact_once_after_revision_advanced(self):
        plan = self.service.resolve_scene_patch([{'action':'place','key':'a','model':1000,
                    'x':0,'y':0,'z':0,'heading':0,'snap':False}])
        self.service.apply_resolved_plan(plan['plan_id'])
        self.assertTrue(self.service.apply_resolved_plan(plan['plan_id'])['replayed'])
        self.assertEqual(1, len(self.service.enumerate_scene()))

    def test_quick_discovery_honours_style_and_avoid(self):
        candidates = [dict(id=1,name='interior chair',style_tags='weathered'),
                      dict(id=2,name='chair',style_tags='urban'),
                      dict(id=3,name='chair',style_tags='weathered')]
        with patch.object(self.service, 'search_assets', return_value=candidates):
            result = self.service.discover_assets('chair', styles=['weathered'], avoid=['interior'], limit=2, detail='full')
        self.assertEqual([3,2], [a['id'] for a in result['assets']])

    def test_variant_diff_uses_stable_keys(self):
        self.place('a')
        self.service.checkpoint_save('a')
        self.place('b', 8)
        self.service.checkpoint_save('b')
        result = self.service.vibe.compare_variants('a','b')
        self.assertEqual(['b'], result['added'])

    def test_annotated_anchor_aligns_in_parent_coordinates(self):
        self.place('a', 10)
        self.service.apply_scene_patch([{'action':'transform','key':'a','x':10,'y':20,'z':0,
                                        'heading':90,'snap':False}])
        profiles = Mock()
        profiles.resolve_anchor.side_effect = lambda model, name: {'mount':[1,0,.5], 'base':[0,0,.5]}[name]
        profiles.get.return_value = {'annotation':{'features':{'anchors':{'mount':[1,0,.5], 'base':[0,0,.5]}}}}
        with patch.object(ArianeService, 'profiles', new_callable=PropertyMock, return_value=profiles):
            plan = self.service.resolve_scene_patch([{'action':'place_relative','key':'b','model':1001,
                   'relative_to':'a','parent_anchor':'mount','child_anchor':'base','snap':False}])
        op=plan['operations'][0]
        self.assertAlmostEqual(10,op['x'])
        self.assertAlmostEqual(21,op['y'])
        self.assertAlmostEqual(0,op['z'])

    def test_bad_zone_rejected_without_changing_project(self):
        with self.assertRaisesRegex(ValueError,'zone'):
            self.service.vibe.update_project({'zones':[{'shape':'rect','x':0,'y':0,'width':-1,'depth':3}]})
        self.assertEqual(0,self.service.vibe.project()['revision'])

    def test_profile_keeps_unknown_front_explicit(self):
        index=Mock()
        index.inspect.return_value={'id':1,'name':'test','width':1,'depth':1,'height':1}
        with patch.object(ArianeService, 'assets', new_callable=PropertyMock, return_value=index):
            profile=self.service.profiles.get(1)
            self.assertIn('front_heading',profile['unknowns'])
            self.service.profiles.annotate(1,{'materials':['wood'],'styles':['weathered']},'preview.png',.8)
            self.assertIn(1,[item['asset_id'] for item in self.service.profiles.search('wood')])
            self.assertIn('front_heading',self.service.profiles.get(1)['unknowns'])

    def test_reference_search_uses_real_local_descriptors(self):
        from PIL import Image
        images=self.root/'images'
        images.mkdir()
        Image.new('RGB',(16,16),(255,0,0)).save(images/'1.png')
        Image.new('RGB',(16,16),(0,0,255)).save(images/'2.png')
        index=Mock()
        index.catalog.return_value=[{'id':1},{'id':2}]
        with patch.object(ArianeService, 'assets', new_callable=PropertyMock, return_value=index):
            self.service.profiles.index_images(str(images))
            result=self.service.profiles.reference_search(str(images/'1.png'))
        self.assertEqual(1,result['assets'][0]['asset_id'])
        self.assertTrue(result['requires_visual_review'])


class RecipeTests(unittest.TestCase):
    def test_terrace_reserves_central_aisle_and_faces_seats(self):
        result = build_recipe('terrace', {'table':1,'seat':2}, {'table':[2,1,1],'seat':[.5,.5,1]},
                              key='terrace',x=0,y=0,count=2,aisle=3)
        self.assertEqual(12, len(result['operations']))
        for op in result['operations']:
            self.assertGreater(abs(op['x']), 1.5)
        seats = [op for op in result['operations'] if op['model']==2]
        self.assertEqual({0,180}, {o['heading'] for o in seats})

    def test_market_opposes_fronts_and_keeps_stock_behind(self):
        result = build_recipe('market', {'counter':1,'crate':2}, {'counter':[3,1,1],'crate':[1,1,1]},
                              key='market',x=0,y=0,count=1)
        self.assertEqual(4,len(result['operations']))
        for counter,crate in zip(result['operations'][::2],result['operations'][1::2]):
            self.assertGreater(abs(crate['x']), abs(counter['x']))

    def test_fence_preserves_gate_and_is_deterministic(self):
        args=dict(key='fence',x=0,y=0,points=[[-10,0],[10,0]],gate_width=3)
        a=build_recipe('fence',{'fence':1},{'fence':[2,.1,2]},**args)
        b=build_recipe('fence',{'fence':1},{'fence':[2,.1,2]},**args)
        self.assertEqual(a,b)
        self.assertTrue(all(abs(o['x'])>=2.5 for o in a['operations']))

    def test_oriented_footprints_avoid_aabb_false_positive(self):
        a=rectangle(0,0,10,.2,45)
        b=rectangle(0,2,10,.2,45)
        self.assertFalse(polygons_overlap(a,b))
        self.assertTrue(polygons_overlap(a,rectangle(0,0,1,1,0)))


if __name__=='__main__': unittest.main()
