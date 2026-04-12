#include "euryopa.h"
#include "modloader.h"
#include <algorithm>
#include <ctime>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

GameFile*
NewGameFile(char *path)
{
	GameFile *f = new GameFile;
	f->name = strdup(path);
	const char *src = ModloaderGetSourcePath(path);
	f->sourcePath = src ? strdup(src) : nil;
	return f;
}

namespace FileLoader {

GameFile *currentFile;
void LoadObjectInstance(char *line);

void*
DatDesc::get(DatDesc *desc, const char *name)
{
	for(; desc->name[0] != '\0'; desc++)
		if(strcmp(desc->name, name) == 0)
			return (void*)desc->handler;
	return (void*)desc->handler;
}

static char*
skipWhite(char *s)
{
	while(isspace(*s))
		s++;
	return s;
}

char*
LoadLine(FILE *f)
{
	static char linebuf[1024];
again:
	if(fgets(linebuf, 1024, f) == nil)
		return nil;
	// remove leading whitespace
	char *s = skipWhite(linebuf);
	// remove trailing whitespace
	int end = strlen(s);
	char c;
	while(c = s[--end], isspace(c))
		s[end] = '\0';
	// convert ',' -> ' '
	for(char *t = s; *t; t++)
		if(*t == ',') *t = ' ';
	// don't return empty lines
	if(*s == '\0')
		goto again;
	return s;
}


void
LoadDataFile(const char *filename, DatDesc *desc)
{
	FILE *file;
	char *line;
	void (*handler)(char*) = nil;

	if(file = fopen_ci(filename, "rb"), file == nil)
		return;
log("Loading data file %s\n", filename);
	while(line = LoadLine(file)){
		if(line[0] == '#'){
			// Only inst sections need commented lines so deleted
			// placeholders keep their original indices.
			if(handler == LoadObjectInstance)
				handler(line);
			continue;
		}
		void *tmp = DatDesc::get(desc, line);
		if(tmp){
			handler = (void(*)(char*))tmp;
			continue;
		}
		if(handler)
			handler(line);
	}
	fclose(file);
}

void LoadNothing(char *line) { }

static int firstID, lastID;

void
LoadObject(char *line)
{
	int id;
	char model[MODELNAMELEN];
	char txd[MODELNAMELEN];
	int numAtomics;
	float dist[3];
	int flags;
	int n;

	// SA format
	numAtomics = 1;
	n = sscanf(line, "%d %s %s %f %d", &id, model, txd, dist, &flags);
	if(gameversion != GAME_SA || n != 5 || dist[0] < 4){
		// III and VC format
		sscanf(line, "%d %s %s %d", &id, model, txd, &numAtomics);
		switch(numAtomics){
		case 1:
			sscanf(line, "%d %s %s %d %f %d",
			       &id, model, txd, &numAtomics, dist, &flags);
			break;
		case 2:
			sscanf(line, "%d %s %s %d %f %f %d",
			       &id, model, txd, &numAtomics, dist, dist+1, &flags);
			break;
		case 3:
			sscanf(line, "%d %s %s %d %f %f %f %d",
			       &id, model, txd, &numAtomics, dist, dist+1, dist+2, &flags);
			break;
		}
	}

	ObjectDef *obj = AddObjectDef(id);
	if(obj == nil) return;
	obj->m_type = ObjectDef::ATOMIC;
	strncpy(obj->m_name, model, MODELNAMELEN);
	obj->m_txdSlot = AddTxdSlot(txd);
	obj->m_numAtomics = numAtomics;
	for(n = 0; n < numAtomics; n++)
		obj->m_drawDist[n] = dist[n];
	obj->SetFlags(flags);
	obj->m_isTimed = false;
	if(id > lastID) lastID = id;
	if(id < firstID) firstID = id;

	obj->m_file = currentFile;
}

void
LoadTimeObject(char *line)
{
	int id;
	char model[MODELNAMELEN];
	char txd[MODELNAMELEN];
	int numAtomics;
	float dist[3];
	int flags;
	int timeOn, timeOff;
	int n;

	// SA format
	numAtomics = 1;
	timeOn = 0;
	timeOff = 0;
	n = sscanf(line, "%d %s %s %f %d %d %d", &id, model, txd, dist, &flags, &timeOn, &timeOff);
	if(gameversion != GAME_SA || n != 7 || dist[0] < 4){
		// III and VC format
		sscanf(line, "%d %s %s %d", &id, model, txd, &numAtomics);
		switch(numAtomics){
		case 1:
			sscanf(line, "%d %s %s %d %f %d %d %d",
			       &id, model, txd, &numAtomics, dist, &flags,
			       &timeOn, &timeOff);
			break;
		case 2:
			sscanf(line, "%d %s %s %d %f %f %d %d %d",
			       &id, model, txd, &numAtomics, dist, dist+1, &flags,
			       &timeOn, &timeOff);
			break;
		case 3:
			sscanf(line, "%d %s %s %d %f %f %f %d %d %d",
			       &id, model, txd, &numAtomics, dist, dist+1, dist+2, &flags,
			       &timeOn, &timeOff);
			break;
		}
	}

	ObjectDef *obj = AddObjectDef(id);
	if(obj == nil) return;
	obj->m_type = ObjectDef::ATOMIC;
	strncpy(obj->m_name, model, MODELNAMELEN);
	obj->m_txdSlot = AddTxdSlot(txd);
	obj->m_numAtomics = numAtomics;
	for(n = 0; n < numAtomics; n++)
		obj->m_drawDist[n] = dist[n];
	obj->SetFlags(flags);
	obj->m_isTimed = true;
	obj->m_timeOn = timeOn;
	obj->m_timeOff = timeOff;
	if(id > lastID) lastID = id;
	if(id < firstID) firstID = id;

	obj->m_file = currentFile;
}

void
LoadAnimatedObject(char *line)
{
	int id;
	char model[MODELNAMELEN];
	char txd[MODELNAMELEN];
	char anim[MODELNAMELEN];
	float dist;
	int flags;

	sscanf(line, "%d %s %s %s %f %d", &id, model, txd, anim, &dist, &flags);

	ObjectDef *obj = AddObjectDef(id);
	if(obj == nil) return;
	obj->m_type = ObjectDef::CLUMP;
	strncpy(obj->m_name, model, MODELNAMELEN);
	obj->m_txdSlot = AddTxdSlot(txd);
	strncpy(obj->m_animname, anim, MODELNAMELEN);
	obj->m_numAtomics = 1;	// to make the distance code simpler
	obj->m_drawDist[0] = dist;
	obj->SetFlags(flags);

	obj->m_file = currentFile;
}

static int connectionID = -1;

static PathType pathtype;
static int pathID;

void
LoadPathLine(char *line)
{
	int id;
	char model[MODELNAMELEN];
	char type[20];
	PathNode node;

	memset(&node, 0, sizeof(node));
	node.density = 1.0f;
	if(isIII()){
		if(connectionID == -1){
			sscanf(line, "%s %d %s", type, &pathID, model);
			pathtype = strcmp(type, "ped") == 0 ? PedPath : CarPath;
			connectionID = 0;
		}else{
			sscanf(line, "%d %d %d %f %f %f %f %d %d",
				&node.type, &node.link, &node.linkType,
				&node.x, &node.y, &node.z, &node.width,
				&node.lanesIn, &node.lanesOut);
//			node.width /= 80.0f;	// unused
			if(++connectionID == 12)
				connectionID = -1;
		}
	}else{
		if(connectionID == -1){
			sscanf(line, "%d %d", &pathtype, &pathID);
			connectionID = 0;
		}else{
			sscanf(line, "%d %d %d %f %f %f %f %d %d %d %d %f %d",
				&node.type, &node.link, &node.linkType,
				&node.x, &node.y, &node.z, &node.width,
				&node.lanesIn, &node.lanesOut,
				&node.speed, &node.flags, &node.density,
				&node.special);
			if(++connectionID == 12)
				connectionID = -1;
		}
	}

	node.x /= 16.0f;
	node.y /= 16.0f;
	node.z /= 16.0f;

	if(connectionID)
		Path::AddNode(pathtype, pathID, node);
}

static int
readString(char *line, char *buf)
{
	char *p;
	char *lp = line;
	while(*lp++ != '"');
	p = buf;
	while(*lp != '"') *p++ = *lp++;
	*p = '\0';
	return ++lp-line;
}

void
Load2dEffect(char *line)
{
	Effect e;
	int r,g,b,a;
	int n;

	// no SA support for now
	if(isSA())
		return;

	sscanf(line, "%d %f %f %f %d %d %d %d %d%n",
		&e.id, &e.pos.x, &e.pos.y, &e.pos.z,
		&r, &g, &b, &a, &e.type, &n);
	e.col.red = r;
	e.col.green = g;
	e.col.blue = b;
	e.col.alpha = a;
	line += n;
	switch(e.type){
	case FX_LIGHT:
		line += readString(line, e.light.coronaTex);
		line += readString(line, e.light.shadowTex);
		sscanf(line, "%f %f %f %f %d %d %d %d %d",
			&e.light.lodDist,
			&e.light.size,
			&e.light.coronaSize,
			&e.light.shadowSize,
			&e.light.shadowAlpha,
			&e.light.flashiness,
			&e.light.reflection,
			&e.light.lensFlareType,
			&e.light.flags);
		break;

	case FX_PARTICLE:
		sscanf(line, "%d %f %f %f %f",
			&e.prtcl.particleType,
			&e.prtcl.dir.x,
			&e.prtcl.dir.y,
			&e.prtcl.dir.z,
			&e.prtcl.size);
		break;

	case FX_LOOKATPOINT:
		sscanf(line, "%d %f %f %f %d",
			&e.look.type,
			&e.look.dir.x,
			&e.look.dir.y,
			&e.look.dir.z,
			&e.look.probability);
		break;

	case FX_PEDQUEUE:
		sscanf(line, "%d %f %f %f %f %f %f",
			&e.queue.type,
			&e.queue.queueDir.x,
			&e.queue.queueDir.y,
			&e.queue.queueDir.z,
			&e.queue.useDir.x,
			&e.queue.useDir.y,
			&e.queue.useDir.z);
		break;
	}
	Effects::AddEffect(e);
}

void
LoadTXDParent(char *line)
{
	char child[MODELNAMELEN], parent[MODELNAMELEN];

	sscanf(line, "%s %s", child, parent);
	TxdSetParent(child, parent);
}


static std::vector<ObjectInst*> tmpInsts;
static int iplInstCounter;  // tracks instance index within current IPL file

void
LoadObjectInstance(char *line)
{
	using namespace rw;

	// Deleted instance (commented out) - keep index slot for streaming IPL compatibility
	if(line[0] == '#'){
		tmpInsts.push_back(nil);
		iplInstCounter++;
		return;
	}

	FileObjectInstance fi;

	char model[MODELNAMELEN];
	float areaf;
	float sx, sy, sz;
	int n;

	if(isSA()){
		sscanf(line, "%d %s %d  %f %f %f  %f %f %f %f  %d",
		       &fi.objectId, model, &fi.area,
		       &fi.position.x, &fi.position.y, &fi.position.z,
		       &fi.rotation.x, &fi.rotation.y, &fi.rotation.z, &fi.rotation.w,
		       &fi.lod);
	}else{
		n = sscanf(line, "%d %s %f  %f %f %f  %f %f %f  %f %f %f %f",
		       &fi.objectId, model, &areaf,
		       &fi.position.x, &fi.position.y, &fi.position.z,
		       &sx, &sy, &sz,
		       &fi.rotation.x, &fi.rotation.y, &fi.rotation.z, &fi.rotation.w);
		if(n != 13){
			sscanf(line, "%d %s  %f %f %f  %f %f %f  %f %f %f %f",
			       &fi.objectId, model,
			       &fi.position.x, &fi.position.y, &fi.position.z,
			       &sx, &sy, &sz,
			       &fi.rotation.x, &fi.rotation.y, &fi.rotation.z, &fi.rotation.w);
			areaf = 0.0f;
		}
		fi.area = areaf;
		fi.lod = -1;
	}

	ObjectDef *obj = GetObjectDef(fi.objectId);
	if(obj == nil){
		log("warning: object %d was never defined\n", fi.objectId);
		tmpInsts.push_back(nil);
		iplInstCounter++;
		return;
	}

	ObjectInst *inst = AddInstance();
	inst->Init(&fi);
	inst->m_iplIndex = iplInstCounter++;

	if(!isSA() && obj->m_isBigBuilding)
		inst->SetupBigBuilding();

	if(isSA())
		tmpInsts.push_back(inst);

	inst->m_file = currentFile;
	SetInstIplFilterKey(inst, currentFile ? currentFile->name : nil);
}

void
LoadZone(char *line)
{
	char name[24];
	char text[24];
	int type;
	CBox box;
	int level;
	int n;
	if(line[0] == '#') return;
	n = sscanf(line, "%23s %d %f %f %f %f %f %f %d %23s",
			name, &type,
			&box.min.x, &box.min.y, &box.min.z,
			&box.max.x, &box.max.y, &box.max.z,
			&level, text);
	if(n == 9)
		// in III we add all zones here to the zones array
		Zones::CreateZone(name, type, box, level, nil);
	else if(n == 10)
		Zones::CreateZone(name, type, box, level, text);
}

void
LoadMapZone(char *line)
{
	// Should only be called from III
	// zones always added to mapzone array
	LoadZone(line);
}

void
LoadCullZone(char *line)
{
	rw::V3d center;
	int flags;
	int wantedLevelDrop;

	wantedLevelDrop = 0;
	if(isSA()){
		float s1x, s1y;
		float s2x, s2y;
		float zmin, zmax;
		rw::Plane mirror;
		if(sscanf(line, "%f %f %f  %f %f %f  %f %f %f  %d  %f %f %f %f",
				&center.x, &center.y, &center.z,
				&s1x, &s1y, &zmin,
				&s2x, &s2y, &zmax,
				&flags,
				&mirror.normal.x, &mirror.normal.y, &mirror.normal.z, &mirror.distance) == 14)
			Zones::AddMirrorAttribZone(center, s1x, s1y, s2x, s2y, zmin, zmax, flags, mirror);
		else{
			sscanf(line, "%f %f %f  %f %f %f  %f %f %f  %d %d",
				&center.x, &center.y, &center.z,
				&s1x, &s1y, &zmin,
				&s2x, &s2y, &zmax,
				&flags, &wantedLevelDrop);
			Zones::AddAttribZone(center, s1x, s1y, s2x, s2y, zmin, zmax, flags);
		}
	}else{
		CBox box;
		sscanf(line, "%f %f %f  %f %f %f  %f %f %f  %d %d",
			&center.x, &center.y, &center.z,
			&box.min.x, &box.min.y, &box.min.z,
			&box.max.x, &box.max.y, &box.max.z,
			&flags, &wantedLevelDrop);
		Zones::AddAttribZone(box, flags, wantedLevelDrop);
	}
}

void
LoadTimeCycleModifier(char *line)
{
	CBox box;
	int farclp;
	int extraCol;
	float extraColIntensity;
	float falloffDist;
	float unused;
	float lodDistMult;

	falloffDist = 100.0f;
	unused = 1.0;
	lodDistMult = 1.0f;
	if(sscanf(line, "%f %f %f  %f %f %f  %d  %d %f  %f %f %f",
			&box.min.x, &box.min.y, &box.min.z,
			&box.max.x, &box.max.y, &box.max.z,
			&farclp,
			&extraCol, &extraColIntensity,
			&falloffDist, &unused, &lodDistMult) < 12)
		lodDistMult = unused;
	Timecycle::AddBox(box, farclp, extraCol, extraColIntensity, falloffDist, lodDistMult);
}

DatDesc zoneDesc[] = {
	{ "end", LoadNothing },
	{ "zone", LoadMapZone },
	{ "", nil }
};

DatDesc ideDesc[] = {
	{ "end", LoadNothing },
	{ "objs", LoadObject },
	{ "tobj", LoadTimeObject },
	{ "hier", LoadNothing },
	{ "cars", LoadNothing },
	{ "peds", LoadNothing },
	{ "path", LoadPathLine },
	{ "2dfx", Load2dEffect },
// VC
	{ "weap", LoadNothing },
// SA
	{ "anim", LoadAnimatedObject },
	{ "txdp", LoadTXDParent },
	{ "", nil }
};

DatDesc iplDesc[] = {
	{ "end", LoadNothing },
	{ "inst", LoadObjectInstance },
	{ "zone", LoadZone },
	{ "cull", LoadCullZone },
	{ "pick", LoadNothing },
	{ "path", LoadPathLine },

	{ "occl", LoadNothing },
	{ "mult", LoadNothing },	// actually no-op
	{ "grge", LoadNothing },
	{ "enex", LoadNothing },
	{ "cars", LoadNothing },
	{ "jump", LoadNothing },
	{ "tcyc", LoadTimeCycleModifier },
	{ "auzo", LoadNothing },
	{ "", nil }
};

void
LoadObjectTypes(const char *filename)
{
	int i;
	firstID = 0x7FFFFFFF;
	lastID = -1;
	LoadDataFile(filename, ideDesc);

	/* Finding related models is done per file
	 * but not yet in III */
	if(isIII()){
		firstID = 0;
		lastID = NUMOBJECTDEFS;
	}
	for(i = firstID; i <= lastID; i++)
		if(GetObjectDef(i))
			GetObjectDef(i)->SetupBigBuilding(firstID, lastID);
}

static void
SetupRelatedIPLs(const char *path, int instArraySlot)
{
	const char *filename, *ext, *s;
	char *t, scenename[256];	// maximum is way less anyway....
	int len;
	int i;
	int numRelated = 0;
	IplDef *ipl;

	filename = strrchr(path, '\\');
	if(filename == nil)
		filename = strrchr(path, '/');
	if(filename == nil)
		filename = path - 1;
	ext = strchr(filename+1, '.');
	if(ext == nil)
		return;
	t = scenename;
	for(s = filename+1; s != ext; s++)
		*t++ = *s;
	*t++ = '\0';
	strcat(scenename, "_stream");
	len = strlen(scenename);

	for(i = 0; i < NUMIPLS; i++){
			ipl = GetIplDef(i);
			if(ipl == nil)
				continue;
			if(rw::strncmp_ci(scenename, ipl->name, len) == 0){
				ipl->instArraySlot = instArraySlot;
				LoadIpl(i, path);
				numRelated++;
			}
		}
	log("SetupRelatedIPLs: %s -> %d streamed IPL(s) using slot %d\n",
	    path, numRelated, instArraySlot);
}

// SA only
void
SetupBigBuildings(void)
{
	int i;
	ObjectInst *inst, *lodinst;
	ObjectDef *obj, *lodobj;

	int numTmpInsts = (int)tmpInsts.size();
	for(i = 0; i < numTmpInsts; i++){
		inst = tmpInsts[i];
		if(inst == nil) continue;	// deleted placeholder
		if(inst->m_lodId < 0)
			inst->m_lod = nil;
		else{
			if(inst->m_lodId >= numTmpInsts){
				inst->m_lod = nil;
				continue;
			}
			lodinst = tmpInsts[inst->m_lodId];
			if(lodinst == nil){
				inst->m_lod = nil;	// LOD was deleted
			}else{
				inst->m_lod = lodinst;
				lodinst->m_numChildren++;
			}
		}
	}

	for(i = 0; i < numTmpInsts; i++){
		inst = tmpInsts[i];
		if(inst == nil) continue;	// deleted placeholder
		obj = GetObjectDef(inst->m_objectId);
		if(obj == nil) continue;
		if(obj->m_isBigBuilding || inst->m_numChildren)
			inst->SetupBigBuilding();
		lodinst = inst->m_lod;
		if(lodinst == nil)
			continue;
		lodobj = GetObjectDef(lodinst->m_objectId);
		if(lodobj == nil) continue;
		if(lodinst->m_numChildren == 1 && obj->m_colModel){
			lodobj->m_colModel = obj->m_colModel;
			lodobj->m_gotChildCol = true;
		}
		if(lodobj->m_colModel == nil)
			log("warning: LOD object %s (%d) has no collision\n", lodobj->m_name, lodobj->m_id);
		if(obj->m_colModel == nil)
			log("warning: object %s (%d) has no collision\n", obj->m_name, obj->m_id);
	}
}

void
LoadScene(const char *filename)
{
	tmpInsts.clear();
	iplInstCounter = 0;
	LoadDataFile(filename, iplDesc);

	if(isSA()){
		int i = -1;
		if(tmpInsts.size()){
			i = AddInstArraySlot((int)tmpInsts.size());
			if(i < 0){
				log("LoadScene: failed to allocate scene slot for %s with %d text inst(s)\n",
				    filename, (int)tmpInsts.size());
				return;
			}
			ObjectInst **ia = GetInstArray(i);
			memcpy(ia, tmpInsts.data(), tmpInsts.size()*sizeof(ObjectInst*));
		}

		SetupRelatedIPLs(filename, i);
		SetupBigBuildings();
	}
}

void LoadMapZones(const char *filename) { LoadDataFile(filename, zoneDesc); }


rw::TexDictionary*
LoadTexDictionary(const char *path)
{
	using namespace rw;

	StreamFile stream;
	TexDictionary *txd = nil;
	if(stream.open(getPath(path), "rb")){
		if(findChunk(&stream, rw::ID_TEXDICTIONARY, nil, nil)){
			txd = TexDictionary::streamRead(&stream);
			ConvertTxd(txd);
		}
		stream.close();
	}
	if(txd == nil)
		txd = TexDictionary::create();
	return txd;
}

static void
AddTexDictionaries(rw::TexDictionary *dst, rw::TexDictionary *src)
{
	FORLIST(lnk, src->textures)
		dst->addFront(rw::Texture::fromDict(lnk));
}

void
LoadCollisionFile(const char *path)
{
	FILE *f;
	ColFileHeader colfile;
	int version;
	uint8 *buffer;
	ObjectDef *obj;
 
	f = fopen_ci(path, "rb");
	if(f == nil)
		return;
	while(fread(&colfile, 1, 8, f)){
		version = 0;
		switch(colfile.fourcc){
		case 0x4C4C4F43:	// COLL
			version = 1;
			break;
		case 0x324C4F43:	// COL2
			version = 2;
			break;
		case 0x334C4F43:	// COL3
			version = 3;
			break;
		case 0x344C4F43:	// COL4
			version = 4;
			break;
		default:
			fclose(f);
			return;
		}
		fread(colfile.name, 1, 24, f);
		buffer = rwNewT(uint8, colfile.modelsize-24, 0);
		fread(buffer, 1, colfile.modelsize-24, f);

		obj = GetObjectDef(colfile.name, nil);
		if(obj){
			CColModel *col = new CColModel;
			strncpy(col->name, colfile.name, 24);
			col->file = currentFile;
			obj->m_colModel = col;
			switch(version){
			case 1: ReadColModel(col, buffer, colfile.modelsize-24); break;
			case 2: ReadColModelVer2(col, buffer, colfile.modelsize-24); break;
			case 3: ReadColModelVer3(col, buffer, colfile.modelsize-24); break;
			case 4: ReadColModelVer4(col, buffer, colfile.modelsize-24); break;
			default:
				printf("unknown COL version %d\n", version);
				obj->m_colModel = nil;
			}
		}else
			printf("Couldn't find object %s for collision\n", colfile.name);

		rwFree(buffer);
	}
	fclose(f);
}

static bool haveFinishedDefinitions;
static bool haveLoadedModloaderDefinitionAdditions;

static void
LoadModloaderDefinitionAdditions(void)
{
	if(!ModloaderIsActive() || haveLoadedModloaderDefinitionAdditions)
		return;

	ModloaderDatEntry entries[256];
	int n = ModloaderGetAdditions(entries, 256);
	for(int i = 0; i < n; i++){
		if(strcmp(entries[i].type, "IDE") == 0){
			currentFile = NewGameFile((char*)entries[i].logicalPath);
			LoadObjectTypes(entries[i].logicalPath);
		}
	}
	for(int i = 0; i < n; i++){
		if(strcmp(entries[i].type, "COLFILE") == 0){
			currentFile = NewGameFile((char*)entries[i].logicalPath);
			LoadCollisionFile(entries[i].logicalPath);
		}
	}
	haveLoadedModloaderDefinitionAdditions = true;
}

void
LoadLevel(const char *filename)
{
	FILE *file;
	char *line;
	char path[256];
	rw::TexDictionary *curTxd;


	if(file = fopen_ci(filename, "rb"), file == nil)
		return;

	curTxd = rw::TexDictionary::getCurrent();
	while(line = LoadLine(file)){
		if(line[0] == '#')
			continue;
		if(strncmp(line, "EXIT", 4) == 0)
			break;
		else if(strncmp(line, "IMAGEPATH", 9) == 0){
			strcpy(path, line+10);
			strcat(path, "/");
			rw::Image::setSearchPath(path);
		}else if(strncmp(line, "TEXDICTION", 10) == 0){
			rw::TexDictionary *txd;
			txd = LoadTexDictionary(line+11);
			AddTexDictionaries(curTxd, txd);
			txd->destroy();
		}else if(strncmp(line, "COLFILE", 7) == 0){
//			eLevelName currlevel = CGame::currLevel;
//			sscanf(line+8, "%d", (int*)&CGame::currLevel);
			strncpy(path, line+10, 256);
			currentFile = NewGameFile(path);
			LoadCollisionFile(path);
//			CGame::currLevel = currlevel;
		}else if(strncmp(line, "MODELFILE", 9) == 0){
			// TODO
//			CFileLoader::LoadModelFile(line+10);
//			debug("MODELFILE\n");
		}else if(strncmp(line, "HIERFILE", 8) == 0){
			// TODO
//			CFileLoader::LoadClumpFile(line+9);
//			debug("HIERFILE\n");
		}else if(strncmp(line, "IDE", 3) == 0){
			strncpy(path, line+4, 256);
			currentFile = NewGameFile(path);
			LoadObjectTypes(path);
		}else if(strncmp(line, "IPL", 3) == 0){
			if(!haveFinishedDefinitions){
				LoadModloaderDefinitionAdditions();
				InitCdImages();
				LoadAllCollisions();
				haveFinishedDefinitions = true;
			}

			strncpy(path, line+4, 256);
			currentFile = NewGameFile(path);
			LoadScene(path);
		}else if(strncmp(line, "MAPZONE", 7) == 0){
//			debug("MAPZONE\n");
			strncpy(path, line+8, 256);
			currentFile = NewGameFile(path);
			LoadMapZones(path);
		}else if(strncmp(line, "SPLASH", 6) == 0){
//			printf("[SPLASH %s]\n", line+7);
		}else if(strncmp(line, "CDIMAGE", 7) == 0){
			AddCdImage(line+8);
		}else if(strncmp(line, "IMG", 3) == 0){
			AddCdImage(line+4);
		}
	}
	fclose(file);
}

static void rememberBinaryImage(int32 *images, int *numImages, int32 imageIndex);
static bool SaveBinaryImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved);
static void WriteInstLine(FILE *f, ObjectInst *inst, int lodIdx, bool deleted);
static bool BuildBinaryImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                                    std::vector<uint8> *outBuffer,
                                    std::vector<std::pair<ObjectInst*, int>> *rebuiltIndexUpdates);
static bool BuildBinaryStandaloneImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                                              std::vector<uint8> *outBuffer,
                                              std::vector<std::pair<ObjectInst*, int>> *rebuiltIndexUpdates);

static int
CollectRelatedStreamingImages(const char *path, int32 *images, int maxImages)
{
	const char *filename, *ext, *s;
	char *t, scenename[256];
	int len;
	int numImages = 0;

	if(path == nil || !isSA())
		return 0;

	filename = strrchr(path, '\\');
	if(filename == nil)
		filename = strrchr(path, '/');
	if(filename == nil)
		filename = path - 1;
	ext = strchr(filename+1, '.');
	if(ext == nil)
		return 0;

	t = scenename;
	for(s = filename+1; s != ext && (t - scenename) < (int)sizeof(scenename)-8; s++)
		*t++ = *s;
	*t = '\0';
	strcat(scenename, "_stream");
	len = strlen(scenename);

	for(int i = 0; i < NUMIPLS; i++){
		IplDef *ipl = GetIplDef(i);
		if(ipl == nil || ipl->imageIndex < 0)
			continue;
		if(rw::strncmp_ci(scenename, ipl->name, len) != 0)
			continue;
		if(numImages < maxImages)
			rememberBinaryImage(images, &numImages, ipl->imageIndex);
	}
	return numImages;
}

static bool
IsImageInList(int32 imageIndex, int32 *images, int numImages)
{
	for(int i = 0; i < numImages; i++)
		if(images[i] == imageIndex)
			return true;
	return false;
}

static bool
ResolveSceneReadPath(const char *filename, char *realpath, size_t realpathSize)
{
	CPtrNode *p;
	const char *srcPath = nil;

	if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
		srcPath = ModloaderGetSourcePath(filename);
		if(srcPath){
			strncpy(realpath, srcPath, realpathSize);
			realpath[realpathSize-1] = '\0';
		}else{
			strncpy(realpath, filename, realpathSize);
			realpath[realpathSize-1] = '\0';
			rw::makePath(realpath);
		}
		return true;
	}

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_file == nil)
			continue;
		if(strcmp(inst->m_file->name, filename) == 0){
			srcPath = inst->m_file->sourcePath;
			break;
		}
	}

	if(srcPath){
		strncpy(realpath, srcPath, realpathSize);
		realpath[realpathSize-1] = '\0';
	}else{
		strncpy(realpath, filename, realpathSize);
		realpath[realpathSize-1] = '\0';
		rw::makePath(realpath);
	}
	return true;
}

static bool
EnsureParentDirectories(const char *path)
{
	if(path == nil || path[0] == '\0')
		return false;

	char dirpath[1024];
	strncpy(dirpath, path, sizeof(dirpath)-1);
	dirpath[sizeof(dirpath)-1] = '\0';
	char *slash = strrchr(dirpath, '/');
#ifdef _WIN32
	char *backslash = strrchr(dirpath, '\\');
	if(backslash && (slash == nil || backslash > slash))
		slash = backslash;
#endif
	if(slash == nil)
		return true;
	*slash = '\0';
	if(dirpath[0] == '\0')
		return true;

	for(char *p = dirpath; *p; p++){
		if(*p != '/' && *p != '\\')
			continue;
		char saved = *p;
		*p = '\0';
		if(dirpath[0] != '\0'){
#ifdef _WIN32
			_mkdir(dirpath);
#else
			mkdir(dirpath, 0777);
#endif
		}
		*p = saved;
	}
#ifdef _WIN32
	_mkdir(dirpath);
#else
	mkdir(dirpath, 0777);
#endif
	return true;
}

static bool
ReplacePath(const char *src, const char *dst)
{
#ifdef _WIN32
	return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(src, dst) == 0;
#endif
}

struct PendingSaveFile
{
	std::string finalPath;
	std::vector<uint8> data;
	std::string tempPath;
	std::string backupPath;
	bool hadOriginal = false;
	bool backupMoved = false;
	bool committed = false;
};

static bool
CommitPendingSaveFiles(std::vector<PendingSaveFile> &files)
{
	for(size_t i = 0; i < files.size(); i++){
		PendingSaveFile &file = files[i];
		if(!EnsureParentDirectories(file.finalPath.c_str())){
			log("SaveScene: failed to create parent directories for %s\n", file.finalPath.c_str());
			return false;
		}
		file.tempPath = file.finalPath + ".ariane.tmp";
		file.backupPath = file.finalPath + ".ariane.bak";
		remove(file.tempPath.c_str());
		remove(file.backupPath.c_str());

		FILE *f = fopen(file.tempPath.c_str(), "wb");
		if(f == nil){
			log("SaveScene: can't create temp file %s\n", file.tempPath.c_str());
			goto fail;
		}
		if(!file.data.empty()){
			size_t written = fwrite(&file.data[0], 1, file.data.size(), f);
			fclose(f);
			if(written != file.data.size()){
				log("SaveScene: short write for temp file %s\n", file.tempPath.c_str());
				goto fail;
			}
		}else
			fclose(f);
	}

	for(size_t i = 0; i < files.size(); i++){
		PendingSaveFile &file = files[i];
		FILE *existing = fopen(file.finalPath.c_str(), "rb");
		if(existing){
			fclose(existing);
			file.hadOriginal = true;
			if(!ReplacePath(file.finalPath.c_str(), file.backupPath.c_str())){
				log("SaveScene: can't move %s to backup %s\n",
				    file.finalPath.c_str(), file.backupPath.c_str());
				goto fail;
			}
			file.backupMoved = true;
		}
	}

	for(size_t i = 0; i < files.size(); i++){
		PendingSaveFile &file = files[i];
		if(!ReplacePath(file.tempPath.c_str(), file.finalPath.c_str())){
			log("SaveScene: can't promote temp file %s to %s\n",
			    file.tempPath.c_str(), file.finalPath.c_str());
			goto fail;
		}
		file.committed = true;
	}

	for(size_t i = 0; i < files.size(); i++)
		if(files[i].backupMoved)
			remove(files[i].backupPath.c_str());
	return true;

fail:
	for(size_t i = 0; i < files.size(); i++){
		PendingSaveFile &file = files[i];
		if(file.committed)
			remove(file.finalPath.c_str());
		if(file.backupMoved){
			ReplacePath(file.backupPath.c_str(), file.finalPath.c_str());
			file.backupMoved = false;
		}
		remove(file.tempPath.c_str());
		remove(file.backupPath.c_str());
	}
	return false;
}

static int
FormatInstLine(char *dst, size_t size, ObjectInst *inst, int lodIdx, bool deleted)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);

	int area = inst->m_area;
	if(isSA()){
		if(inst->m_isUnimportant) area |= 0x100;
		if(inst->m_isUnderWater) area |= 0x400;
		if(inst->m_isTunnel) area |= 0x800;
		if(inst->m_isTunnelTransition) area |= 0x1000;
	}

	const char *prefix = deleted ? "# " : "";
	if(isSA()){
		return snprintf(dst, size, "%s%d, %s, %d, %f, %f, %f, %f, %f, %f, %f, %d\n",
			prefix,
			inst->m_objectId, obj->m_name, area,
			inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
			inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w,
			lodIdx);
	}
	return snprintf(dst, size, "%s%d, %s, %d, %f, %f, %f, 1, 1, 1, %f, %f, %f, %f\n",
		prefix,
		inst->m_objectId, obj->m_name, area,
		inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
		inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w);
}

static void
AppendInstLine(std::string &out, ObjectInst *inst, int lodIdx, bool deleted)
{
	char linebuf[512];
	int written = FormatInstLine(linebuf, sizeof(linebuf), inst, lodIdx, deleted);
	if(written > 0)
		out.append(linebuf, (size_t)written);
}

static bool
BuildSceneFileContents(const char *filename, ObjectInst **insts, int numInsts, bool compactDeletes,
                       std::string &out)
{
	FILE *fin;
	ObjectInst *inst;
	char realpath[1024];

	out.clear();
	ResolveSceneReadPath(filename, realpath, sizeof(realpath));
	fin = fopen(realpath, "rb");
	if(fin){
		char linebuf[1024];
		bool inInstSection = false;
		bool instWritten = false;

		while(fgets(linebuf, sizeof(linebuf), fin)){
			char *s = linebuf;
			while(*s && isspace((unsigned char)*s)) s++;

			if(!inInstSection){
				if(strncmp(s, "inst", 4) == 0 && (s[4] == '\0' || s[4] == '\n' || s[4] == '\r')){
					inInstSection = true;
					out += "inst\n";
					for(int i = 0; i < numInsts; i++){
						inst = insts[i];
						if(compactDeletes && inst->m_isDeleted)
							continue;
						AppendInstLine(out, inst, inst->m_lodId, !compactDeletes && inst->m_isDeleted);
					}
					instWritten = true;
				}else
					out += linebuf;
			}else{
				if(strncmp(s, "end", 3) == 0 && (s[3] == '\0' || s[3] == '\n' || s[3] == '\r')){
					out += "end\n";
					inInstSection = false;
				}
			}
		}
		fclose(fin);

		if(!instWritten){
			out += "inst\n";
			for(int i = 0; i < numInsts; i++){
				inst = insts[i];
				if(compactDeletes && inst->m_isDeleted)
					continue;
				AppendInstLine(out, inst, inst->m_lodId, !compactDeletes && inst->m_isDeleted);
			}
			out += "end\n";
		}
	}else{
		out += "inst\n";
		for(int i = 0; i < numInsts; i++){
			inst = insts[i];
			if(compactDeletes && inst->m_isDeleted)
				continue;
			AppendInstLine(out, inst, inst->m_lodId, !compactDeletes && inst->m_isDeleted);
		}
		out += "end\n";
	}
	return true;
}

static bool
WriteSceneFileInternal(const char *filename, ObjectInst **insts, int numInsts, bool compactDeletes)
{
	std::string out;
	char realpath[1024];
	if(!BuildSceneFileContents(filename, insts, numInsts, compactDeletes, out))
		return false;
	ResolveSceneReadPath(filename, realpath, sizeof(realpath));
	if(!EnsureParentDirectories(realpath))
		return false;

	std::vector<PendingSaveFile> files(1);
	files[0].finalPath = realpath;
	files[0].data.assign(out.begin(), out.end());
	return CommitPendingSaveFiles(files);
}


// Write one instance line to file
static void
WriteInstLine(FILE *f, ObjectInst *inst, int lodIdx, bool deleted)
{
	char linebuf[512];
	int written = FormatInstLine(linebuf, sizeof(linebuf), inst, lodIdx, deleted);
	if(written > 0)
		fwrite(linebuf, 1, (size_t)written, f);
}

static bool
QueueSceneFileSave(const char *filename, ObjectInst **insts, int numInsts, bool compactDeletes,
                   std::vector<PendingSaveFile> &pendingFiles)
{
	std::string out;
	char exportPath[1024];
	if(!BuildSceneFileContents(filename, insts, numInsts, compactDeletes, out))
		return false;
	if(!BuildModloaderLogicalExportPath(filename, exportPath, sizeof(exportPath)))
		return false;

	PendingSaveFile pending;
	pending.finalPath = exportPath;
	pending.data.assign(out.begin(), out.end());
	pendingFiles.push_back(pending);
	return true;
}

// Save all instances that belong to a given IPL file
// Text-only IPLs keep the historical "commented delete" behaviour.
// For SA families with related streaming IPLs, the text file is compacted
// and all related binary LOD indices are remapped before save.
BinaryIplSaveResult
SaveScene(const char *filename)
{
	CPtrNode *p;
	ObjectInst *inst;
	BinaryIplSaveResult result = {};

	// Collect all instances belonging to this file
	int numInsts = 0;
	ObjectInst *fileInsts[8096];
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		if(strcmp(inst->m_file->name, filename) == 0)
			fileInsts[numInsts++] = inst;
	}

	if(numInsts == 0){
		log("SaveScene: no instances found for %s\n", filename);
		return result;
	}

	// Sort by original m_iplIndex
	for(int i = 0; i < numInsts - 1; i++)
		for(int j = i + 1; j < numInsts; j++)
			if(fileInsts[i]->m_iplIndex > fileInsts[j]->m_iplIndex){
				ObjectInst *tmp = fileInsts[i];
				fileInsts[i] = fileInsts[j];
				fileInsts[j] = tmp;
			}

	int32 relatedImages[256];
	int numRelatedImages = CollectRelatedStreamingImages(filename, relatedImages, 256);

	if(numRelatedImages > 0){
		log("SaveScene: family save begin for %s with %d related streaming IPL(s)\n",
			filename, numRelatedImages);
		hotReloadTrace("SaveScene: family save begin for %s with %d related streaming IPL(s)\n",
			filename, numRelatedImages);
		struct SavedTextState
		{
			ObjectInst *inst;
			int oldIplIndex;
			int oldLodId;
			ObjectInst *oldLod;
		};
		struct SavedBinaryState
		{
			ObjectInst *inst;
			int oldLodId;
			ObjectInst *oldLod;
			bool oldDirty;
			int oldBinInstIndex;
		};

		int maxOldIndex = -1;
		for(int i = 0; i < numInsts; i++)
			if(fileInsts[i]->m_iplIndex > maxOldIndex)
				maxOldIndex = fileInsts[i]->m_iplIndex;

		std::vector<int> oldToNew(maxOldIndex+1, -1);
		std::vector<ObjectInst*> oldTextInsts(maxOldIndex+1, nil);
		std::vector<SavedTextState> textStates;
		std::vector<SavedBinaryState> binaryStates;

		ObjectInst *activeTextInsts[8096];
		int numActiveTextInsts = 0;

		for(int i = 0; i < numInsts; i++){
			inst = fileInsts[i];
			if(inst->m_iplIndex >= 0 && inst->m_iplIndex <= maxOldIndex)
				oldTextInsts[inst->m_iplIndex] = inst;
			if(inst->m_isDeleted)
				continue;
			oldToNew[inst->m_iplIndex] = numActiveTextInsts;
			activeTextInsts[numActiveTextInsts++] = inst;
		}

		for(int i = 0; i < numActiveTextInsts; i++){
			inst = activeTextInsts[i];
			textStates.push_back({ inst, inst->m_iplIndex, inst->m_lodId, inst->m_lod });

			int oldLodId = inst->m_lodId;
			if(oldLodId >= 0 &&
			   oldLodId <= maxOldIndex &&
			   oldTextInsts[oldLodId] &&
			   !oldTextInsts[oldLodId]->m_isDeleted){
				inst->m_lod = oldTextInsts[oldLodId];
				inst->m_lodId = oldToNew[oldLodId];
			}else{
				inst->m_lod = nil;
				inst->m_lodId = -1;
			}
		}

		for(p = instances.first; p; p = p->next){
			inst = (ObjectInst*)p->item;
			if(inst->m_imageIndex < 0)
				continue;
			if(!IsImageInList(inst->m_imageIndex, relatedImages, numRelatedImages))
				continue;

			binaryStates.push_back({ inst, inst->m_lodId, inst->m_lod, inst->m_isDirty, inst->m_binInstIndex });

			int oldLodId = inst->m_lodId;
			if(oldLodId >= 0 &&
			   oldLodId <= maxOldIndex &&
			   oldTextInsts[oldLodId] &&
			   !oldTextInsts[oldLodId]->m_isDeleted){
				inst->m_lod = oldTextInsts[oldLodId];
				inst->m_lodId = oldToNew[oldLodId];
			}else{
				inst->m_lod = nil;
				inst->m_lodId = -1;
			}

			if(inst->m_lodId != oldLodId)
				inst->m_isDirty = true;
		}

		int numDeleted = 0;
		int numMoved = 0;
		bool binarySaveFailed = false;
		std::vector<PendingSaveFile> pendingFiles;
		std::vector<std::pair<ObjectInst*, int>> rebuiltIndexUpdates;
		for(int i = 0; i < numRelatedImages; i++){
			GameFile *relatedFile = GetGameFileFromImage(relatedImages[i]);
			log("SaveScene: patching related image %d (%s) for parent %s\n",
				relatedImages[i] & 0xFFFFFF,
				relatedFile && relatedFile->name ? relatedFile->name : "<unknown>",
				filename);
			hotReloadTrace("SaveScene: patching related image %d (%s) for parent %s\n",
				relatedImages[i] & 0xFFFFFF,
				relatedFile && relatedFile->name ? relatedFile->name : "<unknown>",
				filename);
			if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
				std::vector<uint8> writeBuf;
				std::vector<std::pair<ObjectInst*, int>> imageRebuiltUpdates;
				if(!BuildBinaryStandaloneImageByIndex(relatedImages[i], &result, &numDeleted, &numMoved,
				                                      &writeBuf, &imageRebuiltUpdates)){
					binarySaveFailed = true;
					break;
				}
				if(!writeBuf.empty()){
					char exportPath[1024];
					if(!BuildModloaderImageEntryExportPath(relatedImages[i], exportPath, sizeof(exportPath))){
						binarySaveFailed = true;
						result.numFailedFiles++;
						break;
					}

					PendingSaveFile pending;
					pending.finalPath = exportPath;
					pending.data.swap(writeBuf);
					pendingFiles.push_back(pending);
					rebuiltIndexUpdates.insert(rebuiltIndexUpdates.end(),
					                           imageRebuiltUpdates.begin(), imageRebuiltUpdates.end());
				}
			}else if(!SaveBinaryImageByIndex(relatedImages[i], &result, &numDeleted, &numMoved)){
				binarySaveFailed = true;
				break;
			}
		}

		if(!binarySaveFailed){
			if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
				if(!QueueSceneFileSave(filename, fileInsts, numInsts, true, pendingFiles) ||
				   !CommitPendingSaveFiles(pendingFiles)){
					binarySaveFailed = true;
					result.numFailedFiles++;
				}else{
					for(size_t i = 0; i < rebuiltIndexUpdates.size(); i++)
						rebuiltIndexUpdates[i].first->m_binInstIndex = rebuiltIndexUpdates[i].second;
					for(int i = 0; i < numRelatedImages; i++){
						bool hadOutput = false;
						for(size_t pi = 0; pi < pendingFiles.size(); pi++){
							char exportPath[1024];
							if(!BuildModloaderImageEntryExportPath(relatedImages[i], exportPath, sizeof(exportPath)))
								continue;
							if(pendingFiles[pi].finalPath == exportPath){
								hadOutput = true;
								break;
							}
						}
						if(hadOutput)
							rememberBinaryImage(result.savedImages, &result.numSavedImages, relatedImages[i]);
					}
					ModloaderInit();
				}
			}else if(!WriteSceneFileInternal(filename, fileInsts, numInsts, true))
			{
				binarySaveFailed = true;
				result.numFailedFiles++;
			}
		}

		if(binarySaveFailed){
			log("SaveScene: family save failed for %s\n", filename);
			hotReloadTrace("SaveScene: family save failed for %s\n", filename);
			for(size_t i = 0; i < textStates.size(); i++){
				textStates[i].inst->m_iplIndex = textStates[i].oldIplIndex;
				textStates[i].inst->m_lodId = textStates[i].oldLodId;
				textStates[i].inst->m_lod = textStates[i].oldLod;
			}
			for(size_t i = 0; i < binaryStates.size(); i++){
				binaryStates[i].inst->m_lodId = binaryStates[i].oldLodId;
				binaryStates[i].inst->m_lod = binaryStates[i].oldLod;
				binaryStates[i].inst->m_isDirty = binaryStates[i].oldDirty;
				binaryStates[i].inst->m_binInstIndex = binaryStates[i].oldBinInstIndex;
			}
			result.numSavedImages = 0;
			return result;
		}

		log("SaveScene: family save completed for %s\n", filename);
		hotReloadTrace("SaveScene: family save completed for %s\n", filename);

		for(int i = 0; i < numActiveTextInsts; i++)
			activeTextInsts[i]->m_iplIndex = i;
	}else{
		if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
			std::vector<PendingSaveFile> pendingFiles;
			if(!QueueSceneFileSave(filename, fileInsts, numInsts, false, pendingFiles) ||
			   !CommitPendingSaveFiles(pendingFiles)){
				result.numFailedFiles++;
				return result;
			}
			ModloaderInit();
		}else if(!WriteSceneFileInternal(filename, fileInsts, numInsts, false)){
			result.numFailedFiles++;
			return result;
		}
	}

	int numActive = 0;
	for(int i = 0; i < numInsts; i++)
		if(!fileInsts[i]->m_isDeleted) numActive++;
	log("Saved IPL: %s (%d instances, %d active)\n", filename, numInsts, numActive);
	return result;
}

// Save modified instances in binary streaming IPLs by patching
// directly in the IMG archive (in-place, same file size).
// Pure transform/area edits patch the current slots in-place.
// If the live set changed (delete/restore), rebuild the inst section by
// compacting surviving entries to the front and updating indices/counts.
static bool
binaryInstNeedsSave(ObjectInst *inst)
{
	return inst->m_imageIndex >= 0 &&
		(inst->m_isDirty || inst->m_isDeleted != inst->m_wasSavedDeleted);
}

static int
buildBinaryAreaFlags(ObjectInst *inst)
{
	int area = inst->m_area;
	if(inst->m_isUnimportant) area |= 0x100;
	if(inst->m_isUnderWater) area |= 0x400;
	if(inst->m_isTunnel) area |= 0x800;
	if(inst->m_isTunnelTransition) area |= 0x1000;
	return area;
}

static void
fillBinaryInstData(FileObjectInstance *dst, ObjectInst *inst)
{
	dst->position = inst->m_translation;
	dst->rotation = inst->m_rotation;
	dst->objectId = inst->m_objectId;
	dst->area = buildBinaryAreaFlags(inst);
	dst->lod = inst->m_lodId;
}

static void
rememberBinaryImage(int32 *images, int *numImages, int32 imageIndex)
{
	for(int i = 0; i < *numImages; i++)
		if(images[i] == imageIndex)
			return;
	if(*numImages < 256)
		images[(*numImages)++] = imageIndex;
}

struct BinaryIplHeader
{
	uint32 fourcc;
	uint16 numInst;
	uint8 pad06[14];
	uint16 numCarGens;
	uint8 pad16[6];
	uint32 offsetInst;
	uint8 pad20[28];
	uint32 offsetCarGens;
	uint8 pad40[12];
};
static_assert(sizeof(BinaryIplHeader) == 0x4C, "BinaryIplHeader size mismatch");

static bool
BuildBinaryImageByIndexInternal(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                                std::vector<uint8> *outBuffer,
                                std::vector<std::pair<ObjectInst*, int>> *rebuiltIndexUpdates,
                                bool allowStandaloneResize)
{
	CPtrNode *p;
	int size;
	GameFile *imageFile = GetGameFileFromImage(imgIdx);
	const char *imageName = imageFile && imageFile->name ? imageFile->name : "<unknown>";
	uint8 *buffer = ReadFileFromImage(imgIdx, &size);
	if(buffer == nil || *(uint32*)buffer != 0x79726E62){
		log("SaveBinaryIpls: image %d (%s) is not a binary IPL\n",
			imgIdx & 0xFFFFFF, imageName);
		hotReloadTrace("SaveBinaryIpls: image %d (%s) is not a binary IPL\n",
			imgIdx & 0xFFFFFF, imageName);
		rememberBinaryImage(result->failedImages, &result->numFailedImages, imgIdx);
		return false;
	}

	std::vector<uint8> writeBuf(buffer, buffer + size);
	BinaryIplHeader *hdr = (BinaryIplHeader*)&writeBuf[0];
	FileObjectInstance *instData = (FileObjectInstance*)(&writeBuf[0] + hdr->offsetInst);

	std::vector<ObjectInst*> imageInsts;
	imageInsts.reserve(128);
	bool rebuild = false;
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex != imgIdx)
			continue;
		imageInsts.push_back(inst);
		if(inst->m_isDeleted != inst->m_wasSavedDeleted)
			rebuild = true;
	}

	if(imageInsts.empty())
		return true;

	std::sort(imageInsts.begin(), imageInsts.end(),
		[](ObjectInst *a, ObjectInst *b) {
			if(a->m_binInstIndex != b->m_binInstIndex)
				return a->m_binInstIndex < b->m_binInstIndex;
			return a->m_id < b->m_id;
		});

	uint32 nextOffset = (uint32)size;
	if(hdr->offsetCarGens > hdr->offsetInst && hdr->offsetCarGens < nextOffset)
		nextOffset = hdr->offsetCarGens;
	uint32 oldInstBytes = nextOffset > hdr->offsetInst ? nextOffset - hdr->offsetInst : 0;
	uint32 maxInstBytes = oldInstBytes;
	uint32 maxInstCount = maxInstBytes / sizeof(FileObjectInstance);

	bool modified = false;
	std::vector<int> rebuiltIndices;
	if(rebuild){
		int numAlive = 0;
		for(size_t i = 0; i < imageInsts.size(); i++)
			if(!imageInsts[i]->m_isDeleted)
				numAlive++;

		log("SaveBinaryIpls: rebuilding image %d (%s): %d total inst(s), %d alive\n",
			imgIdx & 0xFFFFFF, imageName, (int)imageInsts.size(), numAlive);
		hotReloadTrace("SaveBinaryIpls: rebuilding image %d (%s): %d total inst(s), %d alive\n",
			imgIdx & 0xFFFFFF, imageName, (int)imageInsts.size(), numAlive);

		if(numAlive == 0 && !allowStandaloneResize){
			log("SaveBinaryIpls: refusing to empty binary IPL image %d (%s)\n",
				imgIdx & 0xFFFFFF, imageName);
			hotReloadTrace("SaveBinaryIpls: refusing to empty binary IPL image %d (%s)\n",
				imgIdx & 0xFFFFFF, imageName);
			result->numBlockedEmptyDeletes++;
			return false;
		}
		if((uint32)numAlive > maxInstCount && !allowStandaloneResize){
			log("SaveBinaryIpls: image %d (%s) needs %d inst(s), but only %u fit in section\n",
				imgIdx & 0xFFFFFF, imageName, numAlive, maxInstCount);
			hotReloadTrace("SaveBinaryIpls: image %d (%s) needs %d inst(s), but only %u fit in section\n",
				imgIdx & 0xFFFFFF, imageName, numAlive, maxInstCount);
			rememberBinaryImage(result->failedImages, &result->numFailedImages, imgIdx);
			return false;
		}
		if(allowStandaloneResize){
			size_t newInstBytes = (size_t)numAlive * sizeof(FileObjectInstance);
			size_t suffixBytes = size > (int)nextOffset ? (size_t)size - nextOffset : 0;
			std::vector<uint8> resizedBuf(hdr->offsetInst + newInstBytes + suffixBytes);
			memcpy(&resizedBuf[0], &writeBuf[0], hdr->offsetInst);
			if(suffixBytes > 0)
				memcpy(&resizedBuf[0] + hdr->offsetInst + newInstBytes, &writeBuf[0] + nextOffset, suffixBytes);
			writeBuf.swap(resizedBuf);
			hdr = (BinaryIplHeader*)&writeBuf[0];
			if(nextOffset == hdr->offsetCarGens)
				hdr->offsetCarGens = hdr->offsetInst + (uint32)newInstBytes;
			instData = numAlive > 0 ? (FileObjectInstance*)(&writeBuf[0] + hdr->offsetInst) : nil;
		}

		int nextIdx = 0;
		rebuiltIndices.assign(imageInsts.size(), -1);
		for(size_t i = 0; i < imageInsts.size(); i++){
			ObjectInst *inst = imageInsts[i];
			if(inst->m_isDeleted){
				(*numDeleted)++;
				continue;
			}
			fillBinaryInstData(&instData[nextIdx], inst);
			rebuiltIndices[i] = nextIdx++;
		}
		hdr->numInst = numAlive;
		modified = true;
	}else{
		for(size_t i = 0; i < imageInsts.size(); i++){
			ObjectInst *inst = imageInsts[i];
			if(!inst->m_isDirty)
				continue;
			int idx = inst->m_binInstIndex;
			if(idx < 0 || (uint32)idx >= hdr->numInst){
				log("SaveBinaryIpls: invalid binary inst index %d for image %d (%s)\n",
					idx, imgIdx & 0xFFFFFF, imageName);
				hotReloadTrace("SaveBinaryIpls: invalid binary inst index %d for image %d (%s)\n",
					idx, imgIdx & 0xFFFFFF, imageName);
				rememberBinaryImage(result->failedImages, &result->numFailedImages, imgIdx);
				return false;
			}
			fillBinaryInstData(&instData[idx], inst);
			(*numMoved)++;
			modified = true;
		}
	}

	if(!modified)
		return true;

	memset(hdr->pad20, 0, sizeof(hdr->pad20));

	if(outBuffer){
		outBuffer->swap(writeBuf);
	}

	if(rebuild && rebuiltIndexUpdates){
		for(size_t i = 0; i < imageInsts.size(); i++)
			if(rebuiltIndices[i] >= 0)
				rebuiltIndexUpdates->push_back(std::make_pair(imageInsts[i], rebuiltIndices[i]));
	}

	return true;
}

static bool
BuildBinaryImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                        std::vector<uint8> *outBuffer,
                        std::vector<std::pair<ObjectInst*, int>> *rebuiltIndexUpdates)
{
	return BuildBinaryImageByIndexInternal(imgIdx, result, numDeleted, numMoved,
	                                       outBuffer, rebuiltIndexUpdates, false);
}

static bool
BuildBinaryStandaloneImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                                  std::vector<uint8> *outBuffer,
                                  std::vector<std::pair<ObjectInst*, int>> *rebuiltIndexUpdates)
{
	return BuildBinaryImageByIndexInternal(imgIdx, result, numDeleted, numMoved,
	                                       outBuffer, rebuiltIndexUpdates, true);
}

static bool
BuildBinaryBackupImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved,
                              std::vector<uint8> *outBuffer)
{
	return BuildBinaryImageByIndexInternal(imgIdx, result, numDeleted, numMoved,
	                                       outBuffer, nil, true);
}

static bool
SaveBinaryImageByIndex(int32 imgIdx, BinaryIplSaveResult *result, int *numDeleted, int *numMoved)
{
	std::vector<uint8> writeBuf;
	std::vector<std::pair<ObjectInst*, int>> rebuiltIndexUpdates;
	GameFile *imageFile = GetGameFileFromImage(imgIdx);
	const char *imageName = imageFile && imageFile->name ? imageFile->name : "<unknown>";

	if(!BuildBinaryImageByIndex(imgIdx, result, numDeleted, numMoved, &writeBuf, &rebuiltIndexUpdates))
		return false;
	if(writeBuf.empty())
		return true;
	if(!WriteFileToImage(imgIdx, &writeBuf[0], (int)writeBuf.size())){
		log("SaveBinaryIpls: WriteFileToImage failed for image %d (%s)\n",
			imgIdx & 0xFFFFFF, imageName);
		hotReloadTrace("SaveBinaryIpls: WriteFileToImage failed for image %d (%s)\n",
			imgIdx & 0xFFFFFF, imageName);
		rememberBinaryImage(result->failedImages, &result->numFailedImages, imgIdx);
		return false;
	}
	for(size_t i = 0; i < rebuiltIndexUpdates.size(); i++)
		rebuiltIndexUpdates[i].first->m_binInstIndex = rebuiltIndexUpdates[i].second;
	rememberBinaryImage(result->savedImages, &result->numSavedImages, imgIdx);
	log("Patched binary IPL (image %d, %s)\n", imgIdx & 0xFFFFFF, imageName);
	hotReloadTrace("Patched binary IPL (image %d, %s)\n", imgIdx & 0xFFFFFF, imageName);
	return true;
}

BinaryIplSaveResult
SaveBinaryIpls(void)
{
	BinaryIplSaveResult result = {};
	int numDeleted = 0, numMoved = 0;
	int32 imagesToProcess[256];
	int numImagesToProcess = 0;
	CPtrNode *p;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!binaryInstNeedsSave(inst))
			continue;
		rememberBinaryImage(imagesToProcess, &numImagesToProcess, inst->m_imageIndex);
	}

	log("SaveBinaryIpls: %d modified streaming IPL(s) to write back\n", numImagesToProcess);
	if(numImagesToProcess == 0)
		return result;

	for(int ii = 0; ii < numImagesToProcess; ii++)
		SaveBinaryImageByIndex(imagesToProcess[ii], &result, &numDeleted, &numMoved);

	if(numDeleted)
		log("Binary IPLs: %d delete(s) persisted\n", numDeleted);
	if(numMoved)
		log("Binary IPLs: %d updated\n", numMoved);
	if(result.numBlockedEmptyDeletes)
		log("Binary IPLs: %d delete(s) blocked because they would empty a streaming IPL\n",
			result.numBlockedEmptyDeletes);
	if(result.numFailedImages)
		log("Binary IPLs: %d image(s) failed to save\n", result.numFailedImages);
	return result;
}

struct BackupManifestEntry
{
	std::string type;
	std::string logicalPath;
	std::string sourcePath;
	std::string outputPath;
	std::string archiveLogicalPath;
	std::string entryFilename;
};

struct BackupManifestError
{
	std::string scope;
	std::string path;
	std::string reason;
};

static std::string
JoinPathStrings(const std::string &base, const std::string &child)
{
	if(base.empty())
		return child;
	if(child.empty())
		return base;
#ifdef _WIN32
	char sep = '\\';
	if(base.back() == '\\' || base.back() == '/')
		return base + child;
#else
	char sep = '/';
	if(base.back() == '/')
		return base + child;
#endif
	return base + sep + child;
}

static void
NormalizeBackupPath(const char *in, char *out, size_t outSize)
{
	size_t j = 0;
	if(outSize == 0)
		return;
	if(in == nil){
		out[0] = '\0';
		return;
	}
	for(size_t i = 0; in[i] != '\0' && j + 1 < outSize; i++){
		char c = in[i];
		if(c == '\\')
			c = '/';
		if(j == 0 && c == '/')
			continue;
		out[j++] = c;
	}
	out[j] = '\0';
}

static bool
PathExists(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0;
}

static bool
IsDirectoryPath(const char *path)
{
	struct stat st;
	if(path == nil || stat(path, &st) != 0)
		return false;
#ifdef _WIN32
	return (st.st_mode & _S_IFDIR) != 0;
#else
	return S_ISDIR(st.st_mode);
#endif
}

static bool
RemoveDirectoryRecursive(const char *path)
{
	if(path == nil || path[0] == '\0' || !IsDirectoryPath(path))
		return false;

#ifdef _WIN32
	std::string pattern = JoinPathStrings(path, "*");
	WIN32_FIND_DATAA entry;
	HANDLE handle = FindFirstFileA(pattern.c_str(), &entry);
	if(handle != INVALID_HANDLE_VALUE){
		do{
			if(strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0)
				continue;
			std::string childPath = JoinPathStrings(path, entry.cFileName);
			if(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
				if(!RemoveDirectoryRecursive(childPath.c_str())){
					FindClose(handle);
					return false;
				}
			}else if(DeleteFileA(childPath.c_str()) == 0){
				FindClose(handle);
				return false;
			}
		}while(FindNextFileA(handle, &entry));
		FindClose(handle);
	}
	return RemoveDirectoryA(path) != 0;
#else
	DIR *d = opendir(path);
	if(d){
		dirent *ent;
		while((ent = readdir(d)) != nil){
			if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			std::string childPath = JoinPathStrings(path, ent->d_name);
			if(IsDirectoryPath(childPath.c_str())){
				if(!RemoveDirectoryRecursive(childPath.c_str())){
					closedir(d);
					return false;
				}
			}else if(remove(childPath.c_str()) != 0){
				closedir(d);
				return false;
			}
		}
		closedir(d);
	}
	return rmdir(path) == 0;
#endif
}

static void
ListSubdirectories(const char *path, std::vector<std::string> &dirs)
{
	dirs.clear();
	if(path == nil || !IsDirectoryPath(path))
		return;

#ifdef _WIN32
	std::string pattern = JoinPathStrings(path, "*");
	WIN32_FIND_DATAA entry;
	HANDLE handle = FindFirstFileA(pattern.c_str(), &entry);
	if(handle == INVALID_HANDLE_VALUE)
		return;
	do{
		if(strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0)
			continue;
		if(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			dirs.push_back(entry.cFileName);
	}while(FindNextFileA(handle, &entry));
	FindClose(handle);
#else
	DIR *d = opendir(path);
	if(d == nil)
		return;
	dirent *ent;
	while((ent = readdir(d)) != nil){
		if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		std::string childPath = JoinPathStrings(path, ent->d_name);
		if(IsDirectoryPath(childPath.c_str()))
			dirs.push_back(ent->d_name);
	}
	closedir(d);
#endif
}

static void
BuildTimestampString(char *dst, size_t size)
{
	time_t now = time(nil);
	struct tm tmNow;
#ifdef _WIN32
	localtime_s(&tmNow, &now);
#else
	localtime_r(&now, &tmNow);
#endif
	strftime(dst, size, "%Y-%m-%d_%H-%M-%S", &tmNow);
}

static bool
BuildUniqueSnapshotDir(const char *rootDir, char *dst, size_t size)
{
	char timestamp[64];
	BuildTimestampString(timestamp, sizeof(timestamp));
	for(int attempt = 0; attempt < 100; attempt++){
		if(attempt == 0){
			if(snprintf(dst, size, "%s/%s", rootDir, timestamp) >= (int)size)
				return false;
		}else{
			if(snprintf(dst, size, "%s/%s_%02d", rootDir, timestamp, attempt) >= (int)size)
				return false;
		}
		if(!PathExists(dst))
			return true;
	}
	return false;
}

static bool
BuildStreamingFamilyPrefixForBackup(const char *scenePath, char *prefix, size_t size)
{
	const char *filename, *ext, *s;
	char *t;

	if(scenePath == nil || prefix == nil || size < 8)
		return false;

	filename = strrchr(scenePath, '\\');
	if(filename == nil)
		filename = strrchr(scenePath, '/');
	if(filename == nil)
		filename = scenePath - 1;
	ext = strchr(filename+1, '.');
	if(ext == nil)
		return false;

	t = prefix;
	for(s = filename+1; s != ext && (size_t)(t - prefix) < size - 8; s++)
		*t++ = *s;
	*t = '\0';
	strcat(prefix, "_stream");
	return true;
}

static bool
SceneHasRelatedStreamingFamilyForBackup(const char *scenePath)
{
	char prefix[256];
	if(!isSA() || !BuildStreamingFamilyPrefixForBackup(scenePath, prefix, sizeof(prefix)))
		return false;

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0 || inst->m_file == nil)
			continue;
		if(rw::strncmp_ci(inst->m_file->name, prefix, strlen(prefix)) == 0)
			return true;
	}
	return false;
}

static bool
InstanceBelongsToStreamingFamilyForBackup(ObjectInst *inst, const char *scenePath)
{
	char prefix[256];

	if(inst == nil || inst->m_file == nil || scenePath == nil)
		return false;
	if(inst->m_imageIndex < 0)
		return strcmp(inst->m_file->name, scenePath) == 0;
	if(!BuildStreamingFamilyPrefixForBackup(scenePath, prefix, sizeof(prefix)))
		return false;
	return rw::strncmp_ci(inst->m_file->name, prefix, strlen(prefix)) == 0;
}

static bool
textInstNeedsBackup(ObjectInst *inst)
{
	return inst &&
		(inst->m_isDirty ||
		 inst->m_isAdded ||
		 !inst->m_savedStateValid ||
		 inst->m_isDeleted != inst->m_wasSavedDeleted);
}

static bool
sceneNeedsBackup(const char *scenePath)
{
	if(scenePath == nil)
		return false;

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_file == nil || inst->m_imageIndex >= 0)
			continue;
		if(strcmp(inst->m_file->name, scenePath) != 0)
			continue;
		if(textInstNeedsBackup(inst))
			return true;
	}

	if(!SceneHasRelatedStreamingFamilyForBackup(scenePath))
		return false;

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!InstanceBelongsToStreamingFamilyForBackup(inst, scenePath))
			continue;
		if(inst->m_imageIndex >= 0){
			if(binaryInstNeedsSave(inst))
				return true;
		}else if(textInstNeedsBackup(inst))
			return true;
	}
	return false;
}

static bool
BuildBackupTextRelativePath(const char *logicalPath, std::string &relativePath)
{
	char normalized[512];
	NormalizeBackupPath(logicalPath, normalized, sizeof(normalized));
	if(normalized[0] == '\0')
		return false;
	relativePath = "text/";
	relativePath += normalized;
	return true;
}

static bool
BuildBackupBinaryRelativePath(int32 imgIdx, std::string &relativePath, std::string &archiveLogicalPath,
                              std::string &entryFilename)
{
	GameFile *file = GetGameFileFromImage(imgIdx);
	const char *archiveLogicalName = GetCdImageLogicalName(imgIdx);
	char normalizedArchive[512];

	if(file == nil || file->name == nil || archiveLogicalName == nil)
		return false;
	NormalizeBackupPath(archiveLogicalName, normalizedArchive, sizeof(normalizedArchive));
	if(normalizedArchive[0] == '\0')
		return false;

	entryFilename = file->name;
	entryFilename += ".ipl";
	archiveLogicalPath = normalizedArchive;
	relativePath = "streaming/";
	relativePath += archiveLogicalPath;
	relativePath += "/";
	relativePath += entryFilename;
	return true;
}

static bool
BuildBackupSaPathRelativePath(const char *logicalPath, std::string &relativePath)
{
	char normalized[512];
	NormalizeBackupPath(logicalPath, normalized, sizeof(normalized));
	if(normalized[0] == '\0')
		return false;
	relativePath = "sa_paths/";
	relativePath += normalized;
	return true;
}

static bool
IsCustomIplLogicalPath(const char *path)
{
	char normalized[256];
	NormalizeBackupPath(path, normalized, sizeof(normalized));
	return rw::strncmp_ci(normalized, "data/maps/custom.ipl", 20) == 0 &&
	       normalized[20] == '\0';
}

static void
AddBackupError(std::vector<BackupManifestError> &errors, AutomaticBackupResult *result,
               const char *scope, const char *path, const char *reason)
{
	BackupManifestError entry;
	entry.scope = scope ? scope : "";
	entry.path = path ? path : "";
	entry.reason = reason ? reason : "";
	errors.push_back(entry);
	if(result){
		result->hadWarnings = true;
		result->numErrors++;
	}
}

static bool
QueueTextBackupFile(const char *scenePath, ObjectInst **insts, int numInsts, bool compactDeletes,
                    const char *snapshotDir, std::vector<PendingSaveFile> &pendingFiles,
                    std::vector<BackupManifestEntry> &entries, AutomaticBackupResult *result)
{
	std::string out;
	std::string relativePath;
	char sourcePath[1024];

	if(!BuildSceneFileContents(scenePath, insts, numInsts, compactDeletes, out))
		return false;
	if(!BuildBackupTextRelativePath(scenePath, relativePath))
		return false;

	ResolveSceneReadPath(scenePath, sourcePath, sizeof(sourcePath));

	PendingSaveFile pending;
	pending.finalPath = JoinPathStrings(snapshotDir, relativePath);
	pending.data.assign(out.begin(), out.end());
	pendingFiles.push_back(pending);

	BackupManifestEntry entry;
	entry.type = "text";
	entry.logicalPath = scenePath;
	entry.sourcePath = sourcePath;
	entry.outputPath = relativePath;
	entries.push_back(entry);
	if(result)
		result->numTextFiles++;
	return true;
}

static bool
QueueBinaryBackupFile(int32 imgIdx, const std::vector<uint8> &data, const char *snapshotDir,
                      std::vector<PendingSaveFile> &pendingFiles, std::vector<BackupManifestEntry> &entries,
                      AutomaticBackupResult *result)
{
	std::string relativePath, archiveLogicalPath, entryFilename;
	GameFile *file = GetGameFileFromImage(imgIdx);
	const char *sourcePath;

	if(data.empty())
		return true;
	if(!BuildBackupBinaryRelativePath(imgIdx, relativePath, archiveLogicalPath, entryFilename))
		return false;

	sourcePath = file && file->sourcePath ? file->sourcePath : GetCdImageSourcePath(imgIdx);

	PendingSaveFile pending;
	pending.finalPath = JoinPathStrings(snapshotDir, relativePath);
	pending.data = data;
	pendingFiles.push_back(pending);

	BackupManifestEntry entry;
	entry.type = "binary";
	entry.logicalPath = file && file->name ? file->name : "";
	entry.sourcePath = sourcePath ? sourcePath : "";
	entry.outputPath = relativePath;
	entry.archiveLogicalPath = archiveLogicalPath;
	entry.entryFilename = entryFilename;
	entries.push_back(entry);
	if(result)
		result->numBinaryFiles++;
	return true;
}

static bool
QueueSaPathBackupFile(const SAPaths::BackupFile &src, const char *snapshotDir,
                      std::vector<PendingSaveFile> &pendingFiles,
                      std::vector<BackupManifestEntry> &entries,
                      AutomaticBackupResult *result)
{
	std::string relativePath;
	if(src.data.empty())
		return true;
	if(!BuildBackupSaPathRelativePath(src.logicalPath, relativePath))
		return false;

	PendingSaveFile pending;
	pending.finalPath = JoinPathStrings(snapshotDir, relativePath);
	pending.data = src.data;
	pendingFiles.push_back(pending);

	BackupManifestEntry entry;
	entry.type = "sa_path";
	entry.logicalPath = src.logicalPath;
	entry.sourcePath = src.sourcePath;
	entry.outputPath = relativePath;
	entries.push_back(entry);
	if(result)
		result->numBinaryFiles++;
	return true;
}

static bool
QueueSceneBackupSnapshot(const char *filename, const char *snapshotDir,
                         std::vector<PendingSaveFile> &pendingFiles,
                         std::vector<BackupManifestEntry> &entries,
                         std::vector<BackupManifestError> &errors,
                         AutomaticBackupResult *result,
                         int32 *queuedImages, int *numQueuedImages)
{
	CPtrNode *p;
	ObjectInst *inst;
	int numInsts = 0;
	ObjectInst *fileInsts[8096];

	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		if(inst->m_file && strcmp(inst->m_file->name, filename) == 0)
			fileInsts[numInsts++] = inst;
	}

	if(numInsts == 0)
		return true;

	for(int i = 0; i < numInsts - 1; i++)
		for(int j = i + 1; j < numInsts; j++)
			if(fileInsts[i]->m_iplIndex > fileInsts[j]->m_iplIndex){
				ObjectInst *tmp = fileInsts[i];
				fileInsts[i] = fileInsts[j];
				fileInsts[j] = tmp;
			}

	int32 relatedImages[256];
	int numRelatedImages = CollectRelatedStreamingImages(filename, relatedImages, 256);

	if(numRelatedImages == 0)
		return QueueTextBackupFile(filename, fileInsts, numInsts, false, snapshotDir, pendingFiles, entries, result);

	struct SavedTextState
	{
		ObjectInst *inst;
		int oldIplIndex;
		int oldLodId;
		ObjectInst *oldLod;
	};
	struct SavedBinaryState
	{
		ObjectInst *inst;
		int oldLodId;
		ObjectInst *oldLod;
		bool oldDirty;
		int oldBinInstIndex;
	};

	int maxOldIndex = -1;
	for(int i = 0; i < numInsts; i++)
		if(fileInsts[i]->m_iplIndex > maxOldIndex)
			maxOldIndex = fileInsts[i]->m_iplIndex;

	std::vector<int> oldToNew(maxOldIndex+1, -1);
	std::vector<ObjectInst*> oldTextInsts(maxOldIndex+1, nil);
	std::vector<SavedTextState> textStates;
	std::vector<SavedBinaryState> binaryStates;
	ObjectInst *activeTextInsts[8096];
	int numActiveTextInsts = 0;

	for(int i = 0; i < numInsts; i++){
		inst = fileInsts[i];
		if(inst->m_iplIndex >= 0 && inst->m_iplIndex <= maxOldIndex)
			oldTextInsts[inst->m_iplIndex] = inst;
		if(inst->m_isDeleted)
			continue;
		oldToNew[inst->m_iplIndex] = numActiveTextInsts;
		activeTextInsts[numActiveTextInsts++] = inst;
	}

	for(int i = 0; i < numActiveTextInsts; i++){
		inst = activeTextInsts[i];
		textStates.push_back({ inst, inst->m_iplIndex, inst->m_lodId, inst->m_lod });
		int oldLodId = inst->m_lodId;
		if(oldLodId >= 0 &&
		   oldLodId <= maxOldIndex &&
		   oldTextInsts[oldLodId] &&
		   !oldTextInsts[oldLodId]->m_isDeleted){
			inst->m_lod = oldTextInsts[oldLodId];
			inst->m_lodId = oldToNew[oldLodId];
		}else{
			inst->m_lod = nil;
			inst->m_lodId = -1;
		}
	}

	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0)
			continue;
		if(!IsImageInList(inst->m_imageIndex, relatedImages, numRelatedImages))
			continue;

		binaryStates.push_back({ inst, inst->m_lodId, inst->m_lod, inst->m_isDirty, inst->m_binInstIndex });
		int oldLodId = inst->m_lodId;
		if(oldLodId >= 0 &&
		   oldLodId <= maxOldIndex &&
		   oldTextInsts[oldLodId] &&
		   !oldTextInsts[oldLodId]->m_isDeleted){
			inst->m_lod = oldTextInsts[oldLodId];
			inst->m_lodId = oldToNew[oldLodId];
		}else{
			inst->m_lod = nil;
			inst->m_lodId = -1;
		}
		if(inst->m_lodId != oldLodId)
			inst->m_isDirty = true;
	}

	bool ok = true;
	int numDeleted = 0;
	int numMoved = 0;
	int localTextFiles = 0;
	int localBinaryFiles = 0;
	std::vector<PendingSaveFile> familyPendingFiles;
	std::vector<BackupManifestEntry> familyEntries;
	std::vector<int32> familyImages;
	for(int i = 0; i < numRelatedImages && ok; i++){
		std::vector<uint8> writeBuf;
		BinaryIplSaveResult buildResult = {};
		if(!BuildBinaryBackupImageByIndex(relatedImages[i], &buildResult, &numDeleted, &numMoved, &writeBuf)){
			AddBackupError(errors, result, "family", filename, "failed_to_build_related_binary");
			ok = false;
			break;
		}
		AutomaticBackupResult localResult = {};
		if(!QueueBinaryBackupFile(relatedImages[i], writeBuf, snapshotDir, familyPendingFiles, familyEntries, &localResult)){
			AddBackupError(errors, result, "family", filename, "failed_to_queue_related_binary");
			ok = false;
			break;
		}
		localBinaryFiles += localResult.numBinaryFiles;
		familyImages.push_back(relatedImages[i]);
	}

	if(ok){
		AutomaticBackupResult localResult = {};
		if(!QueueTextBackupFile(filename, fileInsts, numInsts, true, snapshotDir, familyPendingFiles, familyEntries, &localResult)){
			AddBackupError(errors, result, "family", filename, "failed_to_queue_text_scene");
			ok = false;
		}else
			localTextFiles += localResult.numTextFiles;
	}

	for(size_t i = 0; i < textStates.size(); i++){
		textStates[i].inst->m_iplIndex = textStates[i].oldIplIndex;
		textStates[i].inst->m_lodId = textStates[i].oldLodId;
		textStates[i].inst->m_lod = textStates[i].oldLod;
	}
	for(size_t i = 0; i < binaryStates.size(); i++){
		binaryStates[i].inst->m_lodId = binaryStates[i].oldLodId;
		binaryStates[i].inst->m_lod = binaryStates[i].oldLod;
		binaryStates[i].inst->m_isDirty = binaryStates[i].oldDirty;
		binaryStates[i].inst->m_binInstIndex = binaryStates[i].oldBinInstIndex;
	}

	if(ok){
		pendingFiles.insert(pendingFiles.end(), familyPendingFiles.begin(), familyPendingFiles.end());
		entries.insert(entries.end(), familyEntries.begin(), familyEntries.end());
		for(size_t i = 0; i < familyImages.size(); i++)
			rememberBinaryImage(queuedImages, numQueuedImages, familyImages[i]);
		if(result){
			result->numTextFiles += localTextFiles;
			result->numBinaryFiles += localBinaryFiles;
		}
	}

	return ok;
}

static bool
QueueStandaloneBinaryBackupSnapshot(int32 imgIdx, const char *snapshotDir,
                                    std::vector<PendingSaveFile> &pendingFiles,
                                    std::vector<BackupManifestEntry> &entries,
                                    std::vector<BackupManifestError> &errors,
                                    AutomaticBackupResult *result)
{
	std::vector<uint8> writeBuf;
	int numDeleted = 0;
	int numMoved = 0;
	BinaryIplSaveResult buildResult = {};

	if(!BuildBinaryBackupImageByIndex(imgIdx, &buildResult, &numDeleted, &numMoved, &writeBuf)){
		const char *logicalName = GetGameFileFromImage(imgIdx) && GetGameFileFromImage(imgIdx)->name ?
			GetGameFileFromImage(imgIdx)->name : "";
		AddBackupError(errors, result, "binary", logicalName, "failed_to_build_binary_snapshot");
		return false;
	}
	if(!QueueBinaryBackupFile(imgIdx, writeBuf, snapshotDir, pendingFiles, entries, result)){
		const char *logicalName = GetGameFileFromImage(imgIdx) && GetGameFileFromImage(imgIdx)->name ?
			GetGameFileFromImage(imgIdx)->name : "";
		AddBackupError(errors, result, "binary", logicalName, "failed_to_queue_binary_snapshot");
		return false;
	}
	return true;
}

static void
BuildBackupManifest(std::string &out, const AutomaticBackupResult &result,
                    const std::vector<BackupManifestEntry> &entries,
                    const std::vector<BackupManifestError> &errors)
{
	out.clear();
	out += "format automatic_backup_v1\n";
	out += "save_destination ";
	out += gSaveDestination == SAVE_DESTINATION_MODLOADER ? "modloader\n" : "original_files\n";
	out += "text_files " + std::to_string(result.numTextFiles) + "\n";
	out += "binary_files " + std::to_string(result.numBinaryFiles) + "\n";
	out += "errors " + std::to_string(result.numErrors) + "\n";

	bool hasCustomIpl = false;
	for(size_t i = 0; i < entries.size(); i++)
		if(entries[i].type == "text" && IsCustomIplLogicalPath(entries[i].logicalPath.c_str())){
			hasCustomIpl = true;
			break;
		}
	out += std::string("custom_ipl_present ") + (hasCustomIpl ? "1\n" : "0\n");
	out += "\n";

	for(size_t i = 0; i < entries.size(); i++){
		const BackupManifestEntry &entry = entries[i];
		out += "entry\n";
		out += "type " + entry.type + "\n";
		out += "logical " + entry.logicalPath + "\n";
		out += "source " + entry.sourcePath + "\n";
		out += "output " + entry.outputPath + "\n";
		if(!entry.archiveLogicalPath.empty())
			out += "archive " + entry.archiveLogicalPath + "\n";
		if(!entry.entryFilename.empty())
			out += "entry_filename " + entry.entryFilename + "\n";
		out += "end\n\n";
	}

	for(size_t i = 0; i < errors.size(); i++){
		const BackupManifestError &error = errors[i];
		out += "error\n";
		out += "scope " + error.scope + "\n";
		out += "path " + error.path + "\n";
		out += "reason " + error.reason + "\n";
		out += "end\n\n";
	}
}

static void
PruneAutomaticBackupSnapshots(const char *rootDir, int keepCount)
{
	if(rootDir == nil || keepCount <= 0)
		return;

	std::vector<std::string> dirs;
	ListSubdirectories(rootDir, dirs);
	if((int)dirs.size() <= keepCount)
		return;

	std::sort(dirs.begin(), dirs.end(), std::greater<std::string>());
	for(size_t i = (size_t)keepCount; i < dirs.size(); i++){
		std::string fullPath = JoinPathStrings(rootDir, dirs[i]);
		RemoveDirectoryRecursive(fullPath.c_str());
	}
}

AutomaticBackupResult
CreateAutomaticBackup(const char *rootDir, int keepCount)
{
	AutomaticBackupResult result = {};
	std::vector<PendingSaveFile> pendingFiles;
	std::vector<BackupManifestEntry> entries;
	std::vector<BackupManifestError> errors;
	std::vector<std::string> checkedScenes;
	int32 queuedImages[256];
	int numQueuedImages = 0;
	char snapshotDir[1024];

	if(rootDir == nil || rootDir[0] == '\0')
		return result;
	if(keepCount < 1)
		keepCount = 1;
	if(!BuildUniqueSnapshotDir(rootDir, snapshotDir, sizeof(snapshotDir))){
		result.hadWarnings = true;
		result.numErrors = 1;
		return result;
	}

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_file == nil || inst->m_imageIndex >= 0)
			continue;

		bool alreadyChecked = false;
		for(size_t i = 0; i < checkedScenes.size(); i++)
			if(checkedScenes[i] == inst->m_file->name){
				alreadyChecked = true;
				break;
			}
		if(alreadyChecked)
			continue;
		checkedScenes.push_back(inst->m_file->name);

		if(sceneNeedsBackup(inst->m_file->name))
			QueueSceneBackupSnapshot(inst->m_file->name, snapshotDir, pendingFiles, entries, errors,
			                         &result, queuedImages, &numQueuedImages);
	}

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || !binaryInstNeedsSave(inst))
			continue;
		if(IsImageInList(inst->m_imageIndex, queuedImages, numQueuedImages))
			continue;
		if(QueueStandaloneBinaryBackupSnapshot(inst->m_imageIndex, snapshotDir, pendingFiles, entries, errors, &result))
			rememberBinaryImage(queuedImages, &numQueuedImages, inst->m_imageIndex);
	}

	if(SAPaths::HasDirtyAreas()){
		std::vector<SAPaths::BackupFile> saPathFiles;
		if(!SAPaths::CollectDirtyAreaBackupFiles(saPathFiles)){
			AddBackupError(errors, &result, "sa_path", "models/gta3.img", "failed_to_collect_dirty_sa_paths");
		}else{
			for(size_t i = 0; i < saPathFiles.size(); i++)
				if(!QueueSaPathBackupFile(saPathFiles[i], snapshotDir, pendingFiles, entries, &result))
					AddBackupError(errors, &result, "sa_path", saPathFiles[i].logicalPath, "failed_to_queue_dirty_sa_path");
		}
	}

	if(pendingFiles.empty() && errors.empty())
		return result;
	if(entries.empty())
		return result;

	std::string manifest;
	BuildBackupManifest(manifest, result, entries, errors);
	PendingSaveFile manifestFile;
	manifestFile.finalPath = JoinPathStrings(snapshotDir, "manifest.txt");
	manifestFile.data.assign(manifest.begin(), manifest.end());
	pendingFiles.push_back(manifestFile);

	if(!CommitPendingSaveFiles(pendingFiles)){
		result.hadWarnings = true;
		result.numErrors++;
		result.createdSnapshot = false;
		result.snapshotPath[0] = '\0';
		return result;
	}

	result.createdSnapshot = true;
	strncpy(result.snapshotPath, snapshotDir, sizeof(result.snapshotPath));
	result.snapshotPath[sizeof(result.snapshotPath)-1] = '\0';
	PruneAutomaticBackupSnapshots(rootDir, keepCount);
	return result;
}

}
