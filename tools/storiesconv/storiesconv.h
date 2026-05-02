#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <stdarg.h>

#include <args.h>
#include <rw.h>

#include <rwgta.h>

#define PS2

using namespace std;
using namespace rw;

#include "rsl.h"

#include "relocchunk.h"

void panic(const char *fmt, ...);

extern bool32 RslPSP;
extern int32 atmOffset;
extern bool32 unswizzle;
TexDictionary *convertTXD(RslTexList *txd);
RslElement *makeTextures(RslElement *atomic, void*);
RslTexture *dumpTextureCB(RslTexture *texture, void*);
Atomic *convertAtomic(RslElement *atomic);
Atomic *convertAtomic(RslElement *atomic);
Frame *convertFrame(RslNode *f);
Clump *convertClump(RslElementGroup *c);

void moveAtomics(Frame *f);

const char *lookupHashKey(uint32 key);
uint32 GetKey(const char *str, int len);
uint32 GetUppercaseKey(const char *str, int len);
uint32 CalcHashKey(const char *str, int len);
uint32 CalcHashKey(const char *str);
