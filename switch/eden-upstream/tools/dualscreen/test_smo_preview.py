import math
import json
import struct
import tempfile
from pathlib import Path
import unittest
from prepare_smo_preview import decompose, apply_native_visibility, native_body_parts, animate
from smo_preview_catalog import attachment, appearance, build_appearances


class PreviewTests(unittest.TestCase):
    def test_native_static_uniform_material_has_valid_optional_collections(self):
        doc = {'asset':{'version':'2.0'}, 'nodes':[{'name':'Root'}], 'buffers':[{'byteLength':4}],
               'images':[], 'textures':[], 'samplers':[], 'animations':[]}
        encoded = json.dumps(doc).encode()
        encoded += b' ' * (-len(encoded) % 4)
        blob = struct.pack('<III',0x46546c67,2,32+len(encoded))
        blob += struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',4,0x004e4942)+b'\0'*4
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'NativeRigidPart.glb'
            output = Path(directory)/'packed.glb'
            source.write_bytes(blob)
            animate(source,output,native_static=True,preserve_hair=True)
            result = output.read_bytes()
            packed = json.loads(result[20:20+struct.unpack_from('<I',result,12)[0]])
            for key in ('images','textures','samplers','animations'):
                self.assertNotIn(key,packed)
            self.assertTrue(packed['extras']['smoPreview']['static'])

    def catalog_fixture(self):
        def body(**flags):
            return {'metadata': {'InitPartsFixInfoHead.byml': {'JointName': 'Head'},
                'InitPartsFixInfoHair.byml': {'JointName': 'Head'},
                'InitCostumeInfoBody.byml': flags}}
        def head(**flags):
            return {'metadata': {'InitCostumeInfoHead.byml': flags}}
        return {'Mario': body(), 'Mario64': body(IsMario64=True, IsUseHeadSuffix=True,
                                                IsHideHeadHair=True, IsUseBodyHair=True),
                'Mario64Metal': body(IsMario64=True), 'MarioHead': head(),
                'Mario64Head': head(IsMario64=True), 'Mario64HeadOther': head(IsMario64=True),
                'Mario64HeadMetal': head(IsMario64=True)}

    def test_cross_costume_hat_uses_native_variant(self):
        native = self.catalog_fixture()
        self.assertEqual(appearance(native, 'Mario', 'Mario64')['parts'][-1]['model'], 'Mario64HeadOther')
        self.assertEqual(appearance(native, 'Mario64', 'Mario64')['parts'][-1]['model'], 'Mario64Head')
        self.assertEqual(appearance(native, 'Mario64Metal', 'Mario64')['parts'][-1]['model'], 'Mario64HeadMetal')

    def test_mario64_uses_body_hair_only_with_non64_hat(self):
        native = self.catalog_fixture()
        own = appearance(native, 'Mario64', 'Mario64')['parts']
        mixed = appearance(native, 'Mario64', 'Mario')['parts']
        self.assertEqual(len(own), 1)
        self.assertEqual(mixed[-1]['model'], 'Mario64Hair')
        self.assertEqual(mixed[0]['hidden'], ['Hair','CapHair'])

    def test_body_specific_hat_transform_and_scale(self):
        native = self.catalog_fixture()
        native['Mario64']['metadata']['InitPartsFixInfoHeadMario.byml'] = {
            'JointName':'Head', 'LocalTrans':{'Y':5}, 'LocalScale':{'X':1.2,'Y':1.3,'Z':1.4},
            'UseLocalScale':True}
        part = appearance(native, 'Mario64', 'Mario')['parts'][0]
        self.assertEqual(part['translation'], [0,5,0])
        self.assertEqual(part['scale'], [1.2,1.3,1.4])
        self.assertEqual(attachment('Test','Test',{'LocalScale':{'X':9}})['scale'], [1,1,1])

    def test_unjointed_native_accessory_keeps_parent_root(self):
        self.assertIsNone(attachment('Decoration','Guitar',{})['joint'])

    def test_invisible_cap_is_native_resource_substitution_not_missing_file(self):
        native = self.catalog_fixture()
        self.assertEqual(appearance(native,'Mario','MarioInvisible')['parts'][0]['model'], 'MarioHead')

    def test_partial_pack_cannot_pass_catalog_validation(self):
        native = self.catalog_fixture()
        with self.assertRaisesRegex(ValueError, 'Incomplete preview'):
            build_appearances(native, {'bodies':['Mario','Mario64'], 'heads':['Mario','Mario64']},
                              {'Mario','MarioHead','Mario64','Mario64Head'})

    def test_costume_body_uses_own_native_parts_and_separate_head(self):
        info = {'InitSubActor.byml': {'CreatorList': [
            {'ClassName': 'PartsModel', 'FixFileSuffixName': 'HandL', 'ModelName': 'MarioHandL'}]},
            'InitPartsFixInfoHandL.byml': {'JointName': 'HandL', 'LocalRotate': {'Y': 90}},
            'InitPartsFixInfoHead.byml': {'JointName': 'Head', 'LocalRotate': {'Y': 270, 'Z': 180}}}
        parts = native_body_parts(info, {'MarioHandL'})
        self.assertEqual([p['slot'] for p in parts], ['HandL', 'Head'])
        self.assertEqual(parts[1]['rotation'], [0, 270, 180])
        self.assertEqual(parts[1]['joint'], 'Armature_Head')
        with self.assertRaises(ValueError): native_body_parts(info, set())

    def test_body_does_not_duplicate_preview_head(self):
        info = {'InitSubActorPauseMenu.byml': {'CreatorList': [
            {'ClassName': 'PartsModel', 'FixFileSuffixName': 'Head', 'ModelName': 'MarioHead'}]},
            'InitPartsFixInfoHead.byml': {'JointName': 'Head'}}
        self.assertEqual(len(native_body_parts(info, set())), 1)
        self.assertIsNone(native_body_parts({'InitSubActor.byml': {'CreatorList': []}}, set()))

    def test_native_wait_hides_eyelid_shapes_not_eyeballs(self):
        names = ['Eyeball', 'EyelidCloseLeft', 'EyelidCloseRight', 'EyelidLeft', 'EyelidRight']
        doc = {'meshes': [{'name': n+'__EyeMT'} for n in names],
               'nodes': [{'mesh': i, 'skin': 0} for i in range(len(names))]}
        apply_native_visibility(doc, {'schema': 1, 'clip': 'Wait', 'frame': 0, 'hiddenBones': names[1:]})
        self.assertEqual(doc['nodes'][0]['mesh'], 0)
        self.assertTrue(all('mesh' not in n and 'skin' not in n for n in doc['nodes'][1:]))

    def test_unmatched_visibility_is_not_silently_ignored(self):
        with self.assertRaises(ValueError):
            apply_native_visibility({'nodes': [], 'meshes': []},
                {'schema': 1, 'clip': 'Wait', 'frame': 0, 'hiddenBones': ['EyelidLeft']})
    def test_native_identity_and_translation(self):
        t,q,s=decompose([1,0,0,0, 0,1,0,0, 0,0,1,0, 4,5,6,1])
        self.assertEqual(t,[4,5,6]);self.assertEqual(q,[0,0,0,1]);self.assertEqual(s,[1,1,1])

    def test_rotation_is_unit_quaternion_not_matrix_blending(self):
        t,q,s=decompose([0,0,-2,0, 0,3,0,0, 4,0,0,0, 0,0,0,1])
        self.assertAlmostEqual(q[1],math.sqrt(.5));self.assertAlmostEqual(q[3],math.sqrt(.5))
        self.assertEqual(s,[2,3,4]);self.assertEqual(t,[0,0,0])

    def test_native_hidden_joint_remains_zero_scale(self):
        _,q,s=decompose([0]*15+[1])
        self.assertEqual(s,[0,0,0]);self.assertEqual(q,[0,0,0,1])

    def test_invalid_animation_fails_before_packaging(self):
        with self.assertRaises(ValueError): decompose([float('nan')]*16)
        with self.assertRaises(ValueError): decompose([1,2,3])

    def test_reflection_retains_signed_scale(self):
        _,q,s=decompose([-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1])
        self.assertEqual(s,[-1,1,1]);self.assertEqual(q,[0,0,0,1])

if __name__=='__main__': unittest.main()
