#include "euryopa.h"
#include "modloader.h"
#include <vector>

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
			// Pass # lines to handler so inst section
			// can count deleted placeholders
			if(handler)
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
	for(i = firstID; i < lastID; i++)
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
			LoadIpl(i);
		}
	}
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
			if(i < 0)
				return;
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


// Write one instance line to file
static void
WriteInstLine(FILE *f, ObjectInst *inst, int lodIdx, bool deleted)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);

	// Reconstruct area flags
	int area = inst->m_area;
	if(isSA()){
		if(inst->m_isUnimportant) area |= 0x100;
		if(inst->m_isUnderWater) area |= 0x400;
		if(inst->m_isTunnel) area |= 0x800;
		if(inst->m_isTunnelTransition) area |= 0x1000;
	}

	const char *prefix = deleted ? "# " : "";

	if(isSA()){
		fprintf(f, "%s%d, %s, %d, %f, %f, %f, %f, %f, %f, %f, %d\n",
			prefix,
			inst->m_objectId, obj->m_name, area,
			inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
			inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w,
			lodIdx);
	}else{
		fprintf(f, "%s%d, %s, %d, %f, %f, %f, 1, 1, 1, %f, %f, %f, %f\n",
			prefix,
			inst->m_objectId, obj->m_name, area,
			inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
			inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w);
	}
}

// Save all instances that belong to a given IPL file
// Deleted instances are commented out with #, but stay at their original
// position to preserve LOD index compatibility with binary streaming IPLs.
// On reload, the loader creates nil placeholders for # lines.
void
SaveScene(const char *filename)
{
	FILE *fin, *fout;
	CPtrNode *p;
	ObjectInst *inst;
	char tmppath[1024];

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
		return;
	}

	// Sort by original m_iplIndex
	for(int i = 0; i < numInsts - 1; i++)
		for(int j = i + 1; j < numInsts; j++)
			if(fileInsts[i]->m_iplIndex > fileInsts[j]->m_iplIndex){
				ObjectInst *tmp = fileInsts[i];
				fileInsts[i] = fileInsts[j];
				fileInsts[j] = tmp;
			}

	// Resolve the real OS path — prefer sourcePath from mod files
	char realpath[1024];
	const char *srcPath = nil;
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		if(strcmp(inst->m_file->name, filename) == 0){
			srcPath = inst->m_file->sourcePath;
			break;
		}
	}
	if(srcPath){
		strncpy(realpath, srcPath, sizeof(realpath));
	}else{
		strncpy(realpath, filename, sizeof(realpath));
		rw::makePath(realpath);
	}

	snprintf(tmppath, sizeof(tmppath), "%s.tmp", realpath);
	fin = fopen(realpath, "rb");
	fout = fopen(tmppath, "w");
	if(fout == nil){
		log("SaveScene: can't create temp file %s\n", tmppath);
		if(fin) fclose(fin);
		return;
	}

	if(fin){
		// Parse original file, replace inst section, copy everything else
		char linebuf[1024];
		bool inInstSection = false;
		bool instWritten = false;

		while(fgets(linebuf, sizeof(linebuf), fin)){
			char *s = linebuf;
			while(*s && isspace((unsigned char)*s)) s++;

			if(!inInstSection){
				if(strncmp(s, "inst", 4) == 0 && (s[4] == '\0' || s[4] == '\n' || s[4] == '\r')){
					inInstSection = true;
					fprintf(fout, "inst\n");
					// Write all instances at their original positions
					// Use m_lodId directly - no remapping needed
					for(int i = 0; i < numInsts; i++){
						inst = fileInsts[i];
						WriteInstLine(fout, inst, inst->m_lodId, inst->m_isDeleted);
					}
					instWritten = true;
				}else{
					fputs(linebuf, fout);
				}
			}else{
				if(strncmp(s, "end", 3) == 0 && (s[3] == '\0' || s[3] == '\n' || s[3] == '\r')){
					fprintf(fout, "end\n");
					inInstSection = false;
				}
			}
		}
		fclose(fin);

		if(!instWritten){
			fprintf(fout, "inst\n");
			for(int i = 0; i < numInsts; i++){
				inst = fileInsts[i];
				WriteInstLine(fout, inst, inst->m_lodId, inst->m_isDeleted);
			}
			fprintf(fout, "end\n");
		}
	}else{
		fprintf(fout, "inst\n");
		for(int i = 0; i < numInsts; i++){
			inst = fileInsts[i];
			WriteInstLine(fout, inst, inst->m_lodId, inst->m_isDeleted);
		}
		fprintf(fout, "end\n");
	}

	fclose(fout);

	remove(realpath);
	rename(tmppath, realpath);

	int numActive = 0;
	for(int i = 0; i < numInsts; i++)
		if(!fileInsts[i]->m_isDeleted) numActive++;
	log("Saved IPL: %s (%d instances, %d active)\n", filename, numInsts, numActive);
}

// Save modified instances in binary streaming IPLs by patching
// directly in the IMG archive (in-place, same file size).
// Only transform/area edits are safe here; deleting binary entries would
// require re-packing the IPL and fixing instance/Lod indices.
void
SaveBinaryIpls(void)
{
	CPtrNode *p;
	int numDeleted = 0, numMoved = 0;

	// Collect all streaming instances that need patching
	struct PatchInfo {
		int32 imageIndex;
		int binInstIndex;
		ObjectInst *inst;
	};
	PatchInfo patches[4096];
	int numPatches = 0;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0) continue;	// text IPL instance
		if(!inst->m_isDeleted && !inst->m_isDirty) continue;	// unmodified
		if(numPatches < 4096){
			patches[numPatches].imageIndex = inst->m_imageIndex;
			patches[numPatches].binInstIndex = inst->m_binInstIndex;
			patches[numPatches].inst = inst;
			numPatches++;
		}
	}

	log("SaveBinaryIpls: %d modified streaming instances to write back\n", numPatches);
	if(numPatches == 0) return;

	// Process each unique imageIndex
	int32 processedImgs[256];
	int numProcessed = 0;

	for(int pi = 0; pi < numPatches; pi++){
		int32 imgIdx = patches[pi].imageIndex;

		// Check if already processed
		bool done = false;
		for(int j = 0; j < numProcessed; j++)
			if(processedImgs[j] == imgIdx){ done = true; break; }
		if(done) continue;
		processedImgs[numProcessed++] = imgIdx;

		// Read the binary IPL
		int size;
		uint8 *buffer = ReadFileFromImage(imgIdx, &size);
		if(*(uint32*)buffer != 0x79726E62) continue;	// not bnry

		FileObjectInstance *instData =
			(FileObjectInstance*)(buffer + *(int32*)(buffer+0x1C));

		// Apply all patches for this imageIndex
		bool modified = false;
		for(int k = 0; k < numPatches; k++){
			if(patches[k].imageIndex != imgIdx) continue;
			int idx = patches[k].binInstIndex;
			ObjectInst *inst = patches[k].inst;

			if(inst->m_isDeleted){
				numDeleted++;
			}else{
				// Write back current position and rotation
				instData[idx].position = inst->m_translation;
				instData[idx].rotation = inst->m_rotation;
				// area flags
				int area = inst->m_area;
				if(inst->m_isUnimportant) area |= 0x100;
				if(inst->m_isUnderWater) area |= 0x400;
				if(inst->m_isTunnel) area |= 0x800;
				if(inst->m_isTunnelTransition) area |= 0x1000;
				instData[idx].area = area;
				numMoved++;
				modified = true;
			}
		}

		if(!modified) continue;

		// Write the modified buffer back to the IMG
		uint8 *writeBuf = rwNewT(uint8, size, 0);
		memcpy(writeBuf, buffer, size);
		WriteFileToImage(imgIdx, writeBuf, size);
		rwFree(writeBuf);

		log("Patched binary IPL (image %d)\n", imgIdx & 0xFFFFFF);
	}

	if(numDeleted)
		log("Binary IPLs: %d delete(s) skipped (hot reload handles runtime removal only)\n", numDeleted);
	if(numMoved)
		log("Binary IPLs: %d updated\n", numMoved);
}

}
