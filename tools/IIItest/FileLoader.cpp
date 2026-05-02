#include "III.h"
#include "FileMgr.h"

char*
CFileLoader::LoadLine(FileHandle f)
{
	static char linebuf[1024];
again:
	// TODO: get rid of fgets
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

// go to next line, comments introduced by ';'
char*
CFileLoader::NextLine(char *line)
{
	for(; *line != '\0'; line++){
		if(*line == ' ' || *line == '\t' || *line == '\r')
			;	// skip whitespace
		else if(*line == ';'){
			// skip comment line
			while(*line != '\n' && *line != '\0')
				line++;
			if(*line == '\0')
				return nil;
		}else
			return line;
	}
	return nil;
}

// skip to next delimiter
char*
CFileLoader::NextDelim(char *line)
{
	while(*line != '\0' && !(*line == ' ' || *line == '\t' || *line == ','))
		line++;
	if(*line == '\0')
		return nil;
	return line;
}

// skip to next delimiter or new line
char*
CFileLoader::NextDelimNL(char *line)
{
	while(*line != '\0' && !(*line == ' ' || *line == '\t' || *line == ',' || *line == '\n'))
		line++;
	if(*line == '\0')
		return nil;
	return line;
}

// skip delimeters to next word
char*
CFileLoader::SkipDelim(char *line)
{
	while(*line == ' ' || *line == '\t' || *line == ',')
		line++;
	if(*line == '\0')
		return nil;
	return line;
}

void
CFileLoader::LoadLevel(const char *filename)
{
	FileHandle file;
	char *line;
	bool objdata = false;
	if(file = CFileMgr::OpenFile(filename, "rb"), file == nil)
		return;
	rw::TexDictionary *curTxd = rw::TexDictionary::getCurrent();
	if(curTxd == nil){
		curTxd = rw::TexDictionary::create();
		rw::TexDictionary::setCurrent(curTxd);
	}
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == '#')
			continue;
		if(strncmp(line, "EXIT", 4) == 0)
			break;
		else if(strncmp(line, "IMAGEPATH", 9) == 0){
			printf("imagepath ");
			puts(line+19);
		}else if(strncmp(line, "TEXDICTION", 10) == 0){
			rw::TexDictionary *txd;
			txd = CFileLoader::LoadTexDictionary(line+11);
			CFileLoader::AddTexDictionaries(curTxd, txd);
			txd->destroy();
		}else if(strncmp(line, "COLFILE", 7) == 0){
			eLevelName currlevel = CGame::currLevel;
			sscanf(line+8, "%d", (int*)&CGame::currLevel);
			CFileLoader::LoadCollisionFile(line+10);
			CGame::currLevel = currlevel;
		}else if(strncmp(line, "MODELFILE", 9) == 0)
			CFileLoader::LoadModelFile(line+10);
		else if(strncmp(line, "HIERFILE", 8) == 0)
			CFileLoader::LoadClumpFile(line+9);
		else if(strncmp(line, "IDE", 3) == 0)
			CFileLoader::LoadObjectTypes(line+4);
		else if(strncmp(line, "IPL", 3) == 0){
			if(!objdata){
				CObjectData::Initialise("DATA\\OBJECT.DAT");
				objdata = true;
			}
			CFileLoader::LoadScene(line+4);
		}else if(strncmp(line, "MAPZONE", 7) == 0)
			CFileLoader::LoadMapZones(line+8);
		else if(strncmp(line, "SPLASH", 6) == 0){
			printf("[SPLASH %s]\n", line+7);
		}
		if(strncmp(line, "CDIMAGE", 7) == 0)
			CdStream::addImage(line+8);
	}
	CFileMgr::CloseFile(file);
	rw::TexDictionary::setCurrent(curTxd);
}

DatDesc CFileLoader::zoneDesc[] = {
	{ "end", CFileLoader::LoadNothing },
	{ "zone", CFileLoader::LoadMapZone },
	{ "", nil }
};

DatDesc CFileLoader::ideDesc[] = {
	{ "end", CFileLoader::LoadNothing },
	{ "objs", CFileLoader::LoadObject },
	{ "tobj", CFileLoader::LoadTimeObject },
	{ "hier", CFileLoader::LoadClumpObject },
	{ "cars", CFileLoader::LoadVehicleObject },
	{ "peds", CFileLoader::LoadPedObject },
	{ "path", CFileLoader::LoadPathLine },
	{ "2dfx", CFileLoader::Load2dEffect },
	{ "", nil }
};

DatDesc CFileLoader::iplDesc[] = {
	{ "end", CFileLoader::LoadNothing },
	{ "inst", CFileLoader::LoadObjectInstance },
	{ "zone", CFileLoader::LoadZone },
	{ "cull", CFileLoader::LoadCullZone },
	{ "pick", CFileLoader::LoadNothing },
	{ "path", CFileLoader::LoadNothing },
	{ "", nil }
};

void
CFileLoader::LoadDataFile(const char *filename, DatDesc *desc)
{
	FileHandle file;
	char *line;
	void (*handler)(char*) = nil;

	if(file = CFileMgr::OpenFile(filename, "rb"), file == nil)
		return;
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == '#')
			continue;
		void *tmp = DatDesc::get(desc, line);
		if(tmp){
			handler = (void(*)(char*))tmp;
			continue;
		}
		if(handler)
			handler(line);
	}
	CFileMgr::CloseFile(file);
}

void
CFileLoader::LoadObjectTypes(const char *filename)
{
	debug("Loading object types from %s...\n", filename);
	CFileLoader::LoadDataFile(filename, ideDesc);

	int i;
	CBaseModelInfo *mi;
	for(i = 0; i < MODELINFOSIZE; i++){
		mi = CModelInfo::GetModelInfo(i);
		if(mi && mi->IsSimple())
			((CSimpleModelInfo*)mi)->SetupBigBuilding();
	}
}

void
CFileLoader::LoadScene(const char *filename)
{
	debug("Creating objects from %s...\n", filename);
	CFileLoader::LoadDataFile(filename, iplDesc);
	debug("Finished loading IPL\n");
}

void
CFileLoader::LoadMapZones(const char *filename)
{
	debug("Creating zones from %s...\n", filename);
	CFileLoader::LoadDataFile(filename, zoneDesc);
	debug("Finished loading IPL\n");
}

//
// IDE
//

void
CFileLoader::LoadObject(char *line)
{
	int id;
	char model[24], txd[24];
	int numObjs;
	float dist[3];
	int flags;
	int furthest;

	sscanf(line, "%d %s %s %d", &id, model, txd, &numObjs);
	switch(numObjs){
	case 1:
		sscanf(line, "%d %s %s %d %f %d",
		       &id, model, txd, &numObjs, dist, &flags);
		furthest = 0;
		break;
	case 2:
		sscanf(line, "%d %s %s %d %f %f %d",
		       &id, model, txd, &numObjs, dist, dist+1, &flags);
		furthest = dist[0] >= dist[1] ? 1 : 0;
		break;
	case 3:
		sscanf(line, "%d %s %s %d %f %f %f %d",
		       &id, model, txd, &numObjs, dist, dist+1, dist+2, &flags);
		// When 1 < 0 < 2, then this is  1 instead of 0...
		furthest = dist[0] >= dist[1] ? 1 :
		           dist[1] >= dist[2] ? 2 : 0;
		break;
	}

	CSimpleModelInfo *modelinfo = CModelInfo::AddSimpleModel(id);
	modelinfo->SetName(model);
	modelinfo->SetNumAtomics(numObjs);
	modelinfo->SetLodDistances(dist);
	modelinfo->m_normalCull   = !!(flags & 0x1);
	modelinfo->m_noFade       = !!(flags & 0x2);
	modelinfo->m_drawLast     = !!(flags & 0x4);
	modelinfo->m_additive     = !!(flags & 0x8);
	modelinfo->m_isSubway     = !!(flags & 0x10);
	modelinfo->m_ignoreLight  = !!(flags & 0x20);
	modelinfo->m_noZwrite     = !!(flags & 0x40);
	modelinfo->m_furthest = furthest;
	modelinfo->SetTexDictionary(txd);

	MatchModelString(model, id);
}

void
CFileLoader::LoadTimeObject(char *line)
{
	int id;
	char model[24], txd[24];
	int numObjs;
	float dist[3];
	int flags;
	int furthest;
	int timeon, timeoff;

	sscanf(line, "%d %s %s %d", &id, model, txd, &numObjs);
	switch(numObjs){
	case 1:
		sscanf(line, "%d %s %s %d %f %d %d %d",
		       &id, model, txd, &numObjs, dist, &flags,
		       &timeon, &timeoff);
		furthest = 0;
		break;
	case 2:
		sscanf(line, "%d %s %s %d %f %f %d %d %d",
		       &id, model, txd, &numObjs, dist, dist+1, &flags,
		       &timeon, &timeoff);
		furthest = dist[0] >= dist[1] ? 1 : 0;
		break;
	case 3:
		sscanf(line, "%d %s %s %d %f %f %f %d %d %d",
		       &id, model, txd, &numObjs, dist, dist+1, dist+2, &flags,
		       &timeon, &timeoff);
		// When 1 < 0 < 2, then this is  1 instead of 0...
		furthest = dist[0] >= dist[1] ? 1 :
		           dist[1] >= dist[2] ? 2 : 0;
		break;
	}

	CTimeModelInfo *modelinfo = CModelInfo::AddTimeModel(id);
	modelinfo->SetName(model);
	modelinfo->SetNumAtomics(numObjs);
	modelinfo->SetLodDistances(dist);
	modelinfo->m_normalCull   = !!(flags & 0x1);
	modelinfo->m_noFade       = !!(flags & 0x2);
	modelinfo->m_drawLast     = !!(flags & 0x4);
	modelinfo->m_additive     = !!(flags & 0x8);
	modelinfo->m_isSubway     = !!(flags & 0x10);
	modelinfo->m_ignoreLight  = !!(flags & 0x20);
	modelinfo->m_noZwrite     = !!(flags & 0x40);
	modelinfo->m_furthest = furthest;
	modelinfo->m_timeOn  = timeon;
	modelinfo->m_timeOff = timeoff;
	modelinfo->SetTexDictionary(txd);

	CTimeModelInfo *other = modelinfo->FindOtherTimeModel();
	if(other)
		other->m_otherTimeModelID = id;

	MatchModelString(model, id);
}

void
CFileLoader::LoadClumpObject(char *line)
{
	int id;
	char model[24], txd[24];
	sscanf(line, "%d %s %s", &id, model, txd);

	CClumpModelInfo *modelinfo = CModelInfo::AddClumpModel(id);
	modelinfo->SetName(model);
	modelinfo->SetTexDictionary(txd);
	modelinfo->SetColModel(&CTempColModels::ms_colModelBBox, false);
}

void
CFileLoader::LoadVehicleObject(char *line)
{
	static StrAssoc types[] = {
		{ "car",   CVehicleModelInfo::Car },
		{ "boat",  CVehicleModelInfo::Boat },
		{ "train", CVehicleModelInfo::Train },
		{ "heli",  CVehicleModelInfo::Heli },
		{ "plane", CVehicleModelInfo::Plane },
		{ "bike",  CVehicleModelInfo::Bike },
		{ "", 0 }
	};
	static StrAssoc classes[] = {
		{ "poorfamily", CVehicleModelInfo::Poorfamily },
		{ "richfamily", CVehicleModelInfo::Richfamily },
		{ "executive",  CVehicleModelInfo::Executive },
		{ "worker",     CVehicleModelInfo::Worker },
		{ "special",    CVehicleModelInfo::Special },
		{ "big",        CVehicleModelInfo::Big },
		{ "taxi",       CVehicleModelInfo::Taxi },
		{ "", 0 }
	};
	int id;
	char model[24], txd[24], vehType[8], handling[16], gameName[32], vehClass[12];
	int frq, lvl, compRules, secondId;
	float wheelScale;
	sscanf(line, "%d %s %s %s %s %s %s %d %d %x %d %f",
	       &id, model, txd, vehType, handling, gameName, vehClass,
	       &frq, &lvl, &compRules, &secondId, &wheelScale);

	CVehicleModelInfo *modelinfo = CModelInfo::AddVehicleModel(id);
	modelinfo->SetName(model);
	modelinfo->SetTexDictionary(txd);
	// TODO frq
	modelinfo->m_handlingId = cHandlingDataMgr::GetHandlingId(handling);
	modelinfo->m_vehicleType = StrAssoc::get(types, vehType);
	modelinfo->m_vehicleClass = StrAssoc::get(classes, vehClass);
	for(char *s = gameName; *s; s++)
		if(*s == '_') *s = ' ';
	strncpy(modelinfo->m_gameName, gameName, 32);
	modelinfo->m_lvl = lvl;
	modelinfo->m_compRules = compRules;
	if(modelinfo->m_vehicleType == CVehicleModelInfo::Car ||
	   modelinfo->m_vehicleType == CVehicleModelInfo::Plane){
		modelinfo->m_extraModelIndex = secondId;
		modelinfo->m_wheelScale = wheelScale;
	}
}

void
CFileLoader::LoadPedObject(char *line)
{
	int id;
	char model[24], txd[24], pedType[24], pedStats[24], animGroup[24];
	int carsCanDrive;
	sscanf(line, "%d %s %s %s %s %s %x",
	       &id, model, txd, pedType, pedStats, animGroup, &carsCanDrive);

	CPedModelInfo *modelinfo = CModelInfo::AddPedModel(id);
	modelinfo->SetName(model);
	modelinfo->SetTexDictionary(txd);
	modelinfo->SetColModel(&CTempColModels::ms_colModelPed1, false);
	modelinfo->m_pedType = CPedType::FindPedType(pedType);
	modelinfo->m_pedStats = CPedStats::GetPedStatType(pedStats);
	for(int i = 0; i < CAnimManager::ms_numAnimAssocDefinitions; i++){
		char *grpname = CAnimManager::GetAnimGroupName(i);
		if(strcmp(grpname, animGroup) == 0){
			modelinfo->m_animGroup = i;
			goto animfound;
		}
	}
	modelinfo->m_animGroup = CAnimManager::ms_numAnimAssocDefinitions;	// invalid
animfound:
	modelinfo->m_carsCanDrive = carsCanDrive;
}

// used by the path functions below
static int pathId, pathItr;

int
CFileLoader::LoadPathHeader(char *line)
{
	char type[20], model[24];
	sscanf(line, "%s %d %s", type, &pathId, model);
	if(strcmp(type, "ped") == 0)
		return 1;
	if(strcmp(type, "car") == 0)
		return 2;
	return -1;
}

void
CFileLoader::LoadPedPathNode(char *line, int id, int i)
{
	int type, next, cross;
	float x, y, z, width;
	sscanf(line, "%d %d %d %f %f %f %f",
		&type, &next, &cross, &x, &y, &z, &width);
	CPathFind::StoreNodeInfoPed(id, i, type, next, (short)x, (short)y, (short)z, 0, !!cross);
}

void
CFileLoader::LoadCarPathNode(char *line, int id, int i)
{
	int type, next, cross;
	float x, y, z, width;
	int left, right;
	sscanf(line, "%d %d %d %f %f %f %f %d %d",
		&type, &next, &cross, &x, &y, &z, &width, &left, &right);
	CPathFind::StoreNodeInfoCar(id, i, type, next, (short)x, (short)y, (short)z, 0, left, right);
}

void
CFileLoader::LoadPathLine(char *line)
{
	static int pathType;
	switch(pathType){
	case 0:
		pathType = CFileLoader::LoadPathHeader(line);
		break;
	case 1:
		CFileLoader::LoadPedPathNode(line, pathId, pathItr);
		pathItr++;
		break;
	case 2:
		CFileLoader::LoadCarPathNode(line, pathId, pathItr);
		pathItr++;
		break;
	}
	// path is done after 12 nodes
	if(pathItr == 12){
		pathType = 0;
		pathItr = 0;
	}
}

void
CFileLoader::Load2dEffect(char *line)
{
	int id, r, g, b, a, type;
	float x, y, z;
	int flag, probability;
	char corona[32], shadow[32], *s;
	int shadowIntens, flash, wet, flare;
	sscanf(line, "%d %f %f %f %d %d %d %d %d",
	       &id, &x, &y, &z, &r, &g, &b, &a, &type);

	CTxdStore::PushCurrentTxd();
	int slot = CTxdStore::FindTxdSlot("particle");
	assert(slot >= 0);
	CTxdStore::SetCurrentTxd(slot);
	CBaseModelInfo *modelinfo;
	C2dEffect *fx;
	modelinfo = CModelInfo::GetModelInfo(id);
	fx = CModelInfo::ms_2dEffectStore.alloc();
	modelinfo->Add2dEffect(fx);
	fx->pos.x = x;
	fx->pos.y = y;
	fx->pos.z = z;
	fx->col.red = r;
	fx->col.green = g;
	fx->col.blue = b;
	fx->col.alpha = a;
	fx->type = type;

	switch(type){
	case 0:	// light
		while(*line++ != '"');
		s = corona;
		while((*s++ = *line++) != '"');
		s[-1] = '\0';
		while(*line++ != '"');
		s = shadow;
		while((*s++ = *line++) != '"');
		s[-1] = '\0';
		sscanf(line, "%f %f %f %f %d %d %d %d %d",
		       &fx->light.dist, &fx->light.outerRange,
		       &fx->light.size, &fx->light.innerRange,
		       &shadowIntens, &flash, &wet, &flare, &flag);
		fx->light.corona = rw::Texture::read(corona, nil);
		fx->light.shadow = rw::Texture::read(shadow, nil);
		fx->light.shadowIntens = shadowIntens;
		fx->light.flash = flash;
		fx->light.wet = wet;
		fx->light.flare = flare;
		fx->light.flag = flag;
		if(flag & 4)
			fx->light.flag &= ~2;
		break;
	case 1: // particles
		sscanf(line, "%d %f %f %f %d %d %d %d %d"
		             " %d %f %f %f %f",
		       &id, &x, &y, &z, &r, &g, &b, &a, &type,
		       &fx->particle.particleType,
		       &fx->particle.dir[0], &fx->particle.dir[1],
		       &fx->particle.dir[2], &fx->particle.scale);
		break;
	case 2: // ped attractor?
		sscanf(line, "%d %f %f %f %d %d %d %d %d"
		             " %d %f %f %f %d",
		       &id, &x, &y, &z, &r, &g, &b, &a, &type,
		       &flag, &fx->attractor.dir[0], &fx->attractor.dir[1],
		       &fx->attractor.dir[2], &probability);
		fx->attractor.flag = flag;
		fx->attractor.probability = probability;
		break;
	}
	CTxdStore::PopCurrentTxd();
}

//
// IPL
//

void
CFileLoader::LoadObjectInstance(char *line)
{
	using namespace rw;

	int id;
	char model[24];
	float sx, sy, sz;
	V3d t;
	Quat q;
	sscanf(line, "%d %s %f %f %f %f %f %f %f %f %f %f",
	       &id, model, &t.x, &t.y, &t.z, &sx, &sy, &sz,
	       &q.x, &q.y, &q.z, &q.w);
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(id);
	if(mi == nil)
		return;
	Matrix *mat = Matrix::create();
	mat->rotate(conj(q), COMBINEREPLACE);
	mat->pos = t;
	if(mi->GetObjectID() == -1){
		CBuilding *build;
		if(id < PATHNODESIZE && CPathFind::DoesObjectHavePath(id)){
			CTreadable *tread = new CTreadable;
			// RegisterMapObject
			build = tread;
		}else
			build = new CBuilding;
		build->SetModelIndexNoCreate(id);
		build->SetTransform(mat);
		build->m_level = CTheZones::GetLevelFromPosition(build->GetPosition());
		if(mi->IsSimple()){
			CSimpleModelInfo *simple = (CSimpleModelInfo*)mi;
			if(simple->m_isBigBuilding)
				build->SetupBigBuilding();
			if(simple->m_isSubway)
				build->m_flagD10 = 1;
			if(simple->GetLargestLodDistance() < 2.0f)
				build->bIsVisible = 0;
		}
		CWorld::Add(build);
	}else{
		CDummyObject *dummy;
		dummy = new CDummyObject;
		dummy->SetModelIndexNoCreate(id);
		dummy->SetTransform(mat);
		CWorld::Add(dummy);
		if(id == MI_GLASS1 ||
		   id == MI_GLASS2 ||
		   id == MI_GLASS3 ||
		   id == MI_GLASS4 ||
		   id == MI_GLASS5 ||
		   id == MI_GLASS6 ||
		   id == MI_GLASS7 ||
		   id == MI_GLASS8)
			dummy->bIsVisible = 0;
		dummy->m_level = CTheZones::GetLevelFromPosition(dummy->GetPosition());
	}
	mat->destroy();
}

void
CFileLoader::LoadZone(char *line)
{
	int type;
	char name[24];
	float minx, miny, minz;
	float maxx, maxy, maxz;
	int level;
	sscanf(line, "%s %d %f %f %f %f %f %f %d",
	       name, &type,
               &minx, &miny, &minz,
               &maxx, &maxy, &maxz, &level);
	CTheZones::CreateZone(name, (eZoneType)type, minx, miny, minz,
		              maxx, maxy, maxz, (eLevelName)level);
}

void
CFileLoader::LoadCullZone(char *line)
{
	CVector center;
	float x1, y1, z1;
	float x2, y2, z2;
	int flag, wanted;
	sscanf(line, "%f %f %f %f %f %f %f %f %f %d %d",
	       &center.x, &center.y, &center.z, &x1, &y1, &z1, &x2, &y2, &z2,
	       &flag, &wanted);
	CCullZones::AddCullZone(center, x1, x2, y1, y2, z1, z2, flag, wanted);
}

//
// ZON
//

void
CFileLoader::LoadMapZone(char *line)
{
	int type;
	char name[24];
	float minx, miny, minz;
	float maxx, maxy, maxz;
	int level;
	sscanf(line, "%s %d %f %f %f %f %f %f %d",
	       name, &type,
               &minx, &miny, &minz,
               &maxx, &maxy, &maxz, &level);
	CTheZones::CreateMapZone(name, (eZoneType)type, minx, miny, minz,
		                 maxx, maxy, maxz, (eLevelName)level);
}

//
// Collision
//

void
CFileLoader::LoadCollisionModel(CColModel *colmodel, rw::uint8 *buf)
{
	float *fp = (float*)buf;
	colmodel->boundingSphere.radius = *fp++;
	colmodel->boundingSphere.center.x = *fp++;
	colmodel->boundingSphere.center.y = *fp++;
	colmodel->boundingSphere.center.z = *fp++;
	colmodel->boundingBox.min.x = *fp++;
	colmodel->boundingBox.min.y = *fp++;
	colmodel->boundingBox.min.z = *fp++;
	colmodel->boundingBox.max.x = *fp++;
	colmodel->boundingBox.max.y = *fp++;
	colmodel->boundingBox.max.z = *fp++;
	buf = (uint8*)fp;
	colmodel->numSpheres = *(int16*)buf;
	buf += 4;
	if(colmodel->numSpheres){
		colmodel->spheres = rwNewT(CColSphere, colmodel->numSpheres, 0);
		for(int i = 0; i < colmodel->numSpheres; i++){
			colmodel->spheres[i].Set(*(float*)buf, *(CVector*)(buf+4), buf[16], buf[17]);
			buf += 20;
		}
	}

	colmodel->numLines = *(int16*)buf;
	buf += 4;
	if(colmodel->numLines){
		colmodel->lines = rwNewT(CColLine, colmodel->numLines, 0);
		for(int i = 0; i < colmodel->numLines; i++){
			colmodel->lines[i].Set(*(CVector*)buf, *(CVector*)(buf+12));
			buf += 24;
		}
	}

	colmodel->numBoxes = *(int16*)buf;
	buf += 4;
	if(colmodel->numBoxes){
		colmodel->boxes = rwNewT(CColBox, colmodel->numBoxes, 0);
		for(int i = 0; i < colmodel->numBoxes; i++){
			colmodel->boxes[i].Set(*(CVector*)buf, *(CVector*)(buf+12), buf[24], buf[25]);
			buf += 28;
		}
	}

	int32 numVertices = *(int16*)buf;
	buf += 4;
	if(numVertices){
		colmodel->vertices = rwNewT(CVector, numVertices, 0);
		for(int i = 0; i < numVertices; i++){
			colmodel->vertices[i] = *(CVector*)buf;
			buf += 12;
		}
	}

	colmodel->numTriangles = *(int16*)buf;
	buf += 4;
	if(colmodel->numTriangles){
		colmodel->triangles = rwNewT(CColTriangle, colmodel->numTriangles, 0);
		for(int i = 0; i < colmodel->numTriangles; i++){
			colmodel->triangles[i].Set(*(int32*)buf, *(int32*)(buf+4), *(int32*)(buf+8), buf[12]);
			buf += 16;
		}
	}
}

#define COLL 0x4C4C4F43

void
CFileLoader::LoadCollisionFile(const char *filename)
{
	struct {
		uint32 ident;
		uint32 size;
	} header;
	FileHandle file;
	char name[24];
	CBaseModelInfo *modelinfo;

	debug("Loading collision file %s\n", filename);
	if(file = CFileMgr::OpenFile(filename, "rb"), file == nil)
		return;
	while(1){
		if(CFileMgr::Read(file, (uint8*)&header, 8) == 0 ||
		   header.ident != COLL)
			return;
		CFileMgr::Read(file, (uint8*)&work_buff, header.size);
		memcpy(name, work_buff, 24);
		modelinfo = CModelInfo::GetModelInfo(name, nil);
		if(modelinfo){
			CColModel *col = modelinfo->GetColModel();
			if(col)
				LoadCollisionModel(col, work_buff+24);
			else{
				col = new CColModel;
				LoadCollisionModel(col, work_buff+24);
				col->level = CGame::currLevel;
				modelinfo->SetColModel(col, true);
			}
		}
	}
	CFileMgr::CloseFile(file);
}

static void
GetNameAndLOD(char *nodename, char *name, int *n)
{
	char *underscore = nil;
	for(char *s = nodename; *s != '\0'; s++){
		if(s[0] == '_' && (s[1] == 'l' || s[1] == 'L'))
			underscore = s;
	}
	if(underscore){
		strncpy(name, nodename, underscore - nodename);
		name[underscore - nodename] = '\0';
		*n = atoi(underscore + 2);
	}else{
		strncpy(name, nodename, 24);
		*n = 0;
	}
}

//
// Misc
//

bool
CFileLoader::LoadAtomicFile(rw::Stream *stream, int id)
{
	CSimpleModelInfo *modelinfo;
	rw::Clump *clump;
	rw::Atomic *atomic;
	char *nodename, name[24];
	int n;
	if(rw::findChunk(stream, rw::ID_CLUMP, nil, nil)){
		clump = rw::Clump::streamRead(stream);
		if(clump == nil)
			return false;
		modelinfo = (CSimpleModelInfo*)CModelInfo::GetModelInfo(id);
		FORLIST(lnk, clump->atomics){
			atomic = rw::Atomic::fromClump(lnk);
			nodename = GetFrameNodeName(atomic->getFrame());
			GetNameAndLOD(nodename, name, &n);
			modelinfo->SetAtomic(n, atomic);
			atomic->clump->removeAtomic(atomic);
			atomic->setFrame(rw::Frame::create());
			CVisibilityPlugins::SetAtomicModelInfo(atomic, modelinfo);
			CVisibilityPlugins::SetAtomicRenderCallback(atomic, nil);
		}
		clump->destroy();
	}
	return true;
}

bool
CFileLoader::LoadClumpFile(rw::Stream *stream, int id)
{
	CClumpModelInfo *modelinfo;
	rw::Clump *clump;
	if(rw::findChunk(stream, rw::ID_CLUMP, nil, nil)){
		clump = rw::Clump::streamRead(stream);
		if(clump == nil)
			return false;
		modelinfo = (CClumpModelInfo*)CModelInfo::GetModelInfo(id);
		modelinfo->SetClump(clump);
		// TODO: ped low detail clump
	}
	return true;

}

void
CFileLoader::LoadClumpFile(const char *filename)
{
	using namespace rw;

	StreamFile stream;
	char *nodename, name[24];
	int n;
	Clump *clump;

	debug("Loading model file %s\n", filename);
	stream.open(getPath(filename), "rb");
	while(findChunk(&stream, rw::ID_CLUMP, nil, nil)){
		clump = Clump::streamRead(&stream);
		if(clump){
			nodename = GetFrameNodeName(clump->getFrame());
			GetNameAndLOD(nodename, name, &n);
			CClumpModelInfo *mi =
			  (CClumpModelInfo*)CModelInfo::GetModelInfo(name, nil);
			if(mi)
				mi->SetClump(clump);
			else
				clump->destroy();
		}
	}
	stream.close();
}

void
CFileLoader::LoadModelFile(const char *filename)
{
	using namespace rw;

	StreamFile stream;
	char *nodename, name[24];
	int n;
	Atomic *atomic;
	Clump *clump;

	debug("Loading model file %s\n", filename);
	stream.open(getPath(filename), "rb");
	if(findChunk(&stream, rw::ID_CLUMP, nil, nil)){
		clump = Clump::streamRead(&stream);
		assert(clump);
		FORLIST(lnk, clump->atomics){
			atomic = rw::Atomic::fromClump(lnk);
			nodename = GetFrameNodeName(atomic->getFrame());
			GetNameAndLOD(nodename, name, &n);
			CSimpleModelInfo *mi =
			  (CSimpleModelInfo*)CModelInfo::GetModelInfo(name, nil);
			if(mi){
				mi->SetAtomic(n, atomic);
				atomic->clump->removeAtomic(atomic);
				atomic->setFrame(Frame::create());
				CVisibilityPlugins::SetAtomicModelInfo(atomic, mi);
				CVisibilityPlugins::SetAtomicRenderCallback(atomic, nil);
			}else
				debug("Can't find Atomic %s\n", name);
		}
		clump->destroy();
	}
	stream.close();
}

rw::TexDictionary*
CFileLoader::LoadTexDictionary(const char *filename)
{
	using namespace rw;

	StreamFile stream;

	debug("Loading texture dictionary file %s\n", filename);
	TexDictionary *txd = nil;
	if(stream.open(getPath(filename), "rb")){
		if(findChunk(&stream, rw::ID_TEXDICTIONARY, nil, nil)){
			txd = TexDictionary::streamRead(&stream);
			convertTxd(txd);
		}
		stream.close();
	}
	if(txd == nil)
		txd = TexDictionary::create();
	return txd;
}

void
CFileLoader::AddTexDictionaries(rw::TexDictionary *dst, rw::TexDictionary *src)
{
	FORLIST(lnk, src->textures)
		dst->addFront(rw::Texture::fromDict(lnk));
}
