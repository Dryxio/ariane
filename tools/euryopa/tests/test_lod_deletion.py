"""Compile the production LOD cascade functions against a minimal scene fixture."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class LodDeletionTests(unittest.TestCase):
    def test_cascade_identity_and_shared_lods(self):
        source = (ROOT / 'tools/euryopa/objectinst.cpp').read_text()
        helpers = source[source.index('static bool\nsameIplFamily'):
                         source.index('void\nObjectInst::UpdateMatrix')]
        cascade = source[source.index('void\nObjectInst::Delete(void)'):
                         source.index('static void\nsetDeletedWithoutCascade')]
        fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#define nil nullptr
namespace rw { int strcmp_ci(const char*a,const char*b){return strcmp(a,b);} }
struct GameFile { const char*name; };
struct ObjectInst {
 int m_iplIndex=0, m_imageIndex=-1, m_lodId=-1;
 ObjectInst*m_lod=nullptr; char m_iplFilterKey[64]="country";
 GameFile*m_file=nullptr;
 bool m_isDeleted=false, m_wasSavedDeleted=false, m_isDirty=false;
 void Delete(); void Undelete(); void Deselect(){}
};
struct CPtrNode { ObjectInst*item; CPtrNode*next; };
struct { CPtrNode*first; } instances;
bool LogicalPathEquals(const char*a,const char*b){return strcmp(a,b)==0;}
bool IsInstInIplMapDocument(ObjectInst*){return true;}
void StampChangeSeq(ObjectInst*){}
#define log printf
'''
        cases = r'''
int main(){
 ObjectInst bin, road, road2, lod, foreign;
 bin.m_imageIndex=1;
 road.m_imageIndex=2; road.m_lodId=0; road.m_lod=&lod;
 road2.m_imageIndex=3; road2.m_lodId=0; road2.m_lod=&lod;
 foreign.m_imageIndex=4; foreign.m_lodId=0;
 strcpy(foreign.m_iplFilterKey,"another_family");
 CPtrNode e{&foreign,nullptr}, d{&lod,&e}, c{&road2,&d}, b{&road,&c}, a{&bin,&b};
 instances.first=&a;
 // Legacy/default index zero on a streamed bin must not impersonate a LOD.
 bin.Delete();
 assert(bin.m_isDeleted && !road.m_isDeleted && !road2.m_isDeleted && !lod.m_isDeleted);
 bin.Undelete();
 assert(!bin.m_isDeleted);
 // Reject an invalid cached binary LOD pointer and recover the text target.
 road.m_lod=&bin;
 assert(resolveInstanceLod(&road)==&lod);
 // A shared LOD stays live until its last child is deleted.
 road.Delete();
 assert(road.m_isDeleted && !road2.m_isDeleted && !lod.m_isDeleted);
 road2.Delete();
 assert(road2.m_isDeleted && lod.m_isDeleted && !foreign.m_isDeleted && !bin.m_isDeleted);
 // Undeleting the real LOD restores its children only.
 lod.Undelete();
 assert(!lod.m_isDeleted && !road.m_isDeleted && !road2.m_isDeleted);
 // Explicitly deleting a real LOD still cascades to its actual children.
 lod.Delete();
 assert(lod.m_isDeleted && road.m_isDeleted && road2.m_isDeleted);
 assert(!foreign.m_isDeleted && !bin.m_isDeleted);
 puts("LOD deletion regression scenarios passed");
}
'''
        with tempfile.TemporaryDirectory(prefix='ariane-lod-test-') as directory:
            cpp = Path(directory) / 'lod_test.cpp'
            exe = Path(directory) / 'lod_test'
            cpp.write_text(fixture + helpers + cascade + cases)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11',
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
