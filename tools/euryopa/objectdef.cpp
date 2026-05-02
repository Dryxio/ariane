#include "euryopa.h"
#include "modloader.h"

static ObjectDef *objdefs[NUMOBJECTDEFS];

static void
destroyDetachedAtomic(rw::Atomic *atomic)
{
	if(atomic == nil)
		return;
	rw::Frame *frame = atomic->getFrame();
	if(frame){
		atomic->setFrame(nil);
		frame->destroyHierarchy();
	}
	atomic->destroy();
}

static void
appendGeometry2dEffects(ObjectDef *obj, rw::Atomic *atomic, rw::Matrix *localXform)
{
	gta::Effect2d *srcEffects;
	int numEffects;

	if(obj == nil || atomic == nil || atomic->geometry == nil)
		return;

	numEffects = gta::getNum2dEffects(atomic->geometry);
	if(numEffects <= 0)
		return;
	srcEffects = gta::get2dEffects(atomic->geometry);
	if(srcEffects == nil)
		return;

	for(int i = 0; i < numEffects; i++){
		const gta::Effect2d *src = &srcEffects[i];
		Effect dst = {};
		rw::V3d tmp;

		dst.id = obj->m_id;
		dst.col = { 255, 255, 255, 255 };
		if(localXform)
			rw::V3d::transformPoints(&dst.pos, &src->posn, 1, localXform);
		else
			dst.pos = src->posn;

		switch(src->type){
		case gta::ET_LIGHT:
			dst.type = FX_LIGHT;
			dst.col = src->attr.l.col;
			dst.light.lodDist = src->attr.l.lodDist;
			dst.light.size = src->attr.l.size;
			dst.light.coronaSize = src->attr.l.coronaSize;
			dst.light.shadowSize = src->attr.l.shadowSize;
			dst.light.flashiness = src->attr.l.flashiness;
			dst.light.reflection = src->attr.l.reflectionType;
			dst.light.lensFlareType = src->attr.l.lensFlareType;
			dst.light.shadowAlpha = src->attr.l.shadowAlpha;
			dst.light.flags = src->attr.l.flags;
			strncpy(dst.light.coronaTex, src->attr.l.coronaTex, sizeof(dst.light.coronaTex)-1);
			strncpy(dst.light.shadowTex, src->attr.l.shadowTex, sizeof(dst.light.shadowTex)-1);
			break;
		case gta::ET_PARTICLE:
			dst.type = FX_PARTICLE;
			dst.col = { 255, 0, 255, 255 };
			strncpy(dst.prtcl.name, src->attr.p.name, sizeof(dst.prtcl.name)-1);
			dst.prtcl.size = 1.0f;
			break;
		case gta::ET_PEDQUEUE:
			dst.type = FX_PEDQUEUE;
			dst.col = { 255, 255, 0, 255 };
			if(localXform){
				rw::V3d::transformVectors(&dst.queue.queueDir, &src->attr.q.queueDir, 1, localXform);
				rw::V3d::transformVectors(&dst.queue.useDir, &src->attr.q.useDir, 1, localXform);
				rw::V3d::transformVectors(&dst.queue.forwardDir, &src->attr.q.forwardDir, 1, localXform);
			}else{
				dst.queue.queueDir = src->attr.q.queueDir;
				dst.queue.useDir = src->attr.q.useDir;
				dst.queue.forwardDir = src->attr.q.forwardDir;
			}
			dst.queue.type = src->attr.q.type;
			dst.queue.interest = src->attr.q.interest;
			dst.queue.lookAt = src->attr.q.lookAt;
			dst.queue.flags = src->attr.q.flags;
			strncpy(dst.queue.scriptName, src->attr.q.scriptName, sizeof(dst.queue.scriptName)-1);
			break;
		case gta::ET_SUNGLARE:
			dst.type = FX_SUNGLARE;
			dst.col = { 255, 255, 0, 255 };
			break;
		case gta::ET_INTERIOR:
			dst.type = FX_INTERIOR;
			dst.col = { 255, 255, 255, 255 };
			dst.interior.type = src->attr.i.type;
			dst.interior.group = src->attr.i.group;
			dst.interior.width = src->attr.i.width;
			dst.interior.depth = src->attr.i.depth;
			dst.interior.height = src->attr.i.height;
			dst.interior.rot = src->attr.i.rot;
			break;
		case gta::ET_ENTRYEXIT:
			dst.type = FX_ENTRYEXIT;
			dst.col = { 255, 128, 0, 255 };
			dst.entryExit.enterAngle = src->attr.e.prot;
			dst.entryExit.radiusX = src->attr.e.wx;
			dst.entryExit.radiusY = src->attr.e.wy;
			tmp = add(src->posn, src->attr.e.spawn);
			if(localXform)
				rw::V3d::transformPoints(&dst.entryExit.exitPos, &tmp, 1, localXform);
			else
				dst.entryExit.exitPos = tmp;
			dst.entryExit.exitAngle = src->attr.e.spawnrot;
			dst.entryExit.areaCode = src->attr.e.areacode;
			dst.entryExit.flags = src->attr.e.flags;
			dst.entryExit.extraColor = src->attr.e.extracol;
			dst.entryExit.openTime = src->attr.e.openTime;
			dst.entryExit.shutTime = src->attr.e.shutTime;
			dst.entryExit.extraFlags = src->attr.e.extraFlags;
			strncpy(dst.entryExit.title, src->attr.e.title, sizeof(dst.entryExit.title)-1);
			break;
		case gta::ET_ROADSIGN:
			dst.type = FX_ROADSIGN;
			dst.col = { 0, 255, 0, 255 };
			dst.roadsign.width = src->attr.rs.width;
			dst.roadsign.height = src->attr.rs.height;
			dst.roadsign.rotX = src->attr.rs.rotX;
			dst.roadsign.rotY = src->attr.rs.rotY;
			dst.roadsign.rotZ = src->attr.rs.rotZ;
			dst.roadsign.flags = src->attr.rs.flags;
			memcpy(dst.roadsign.text, src->attr.rs.text, sizeof(dst.roadsign.text));
			break;
		case gta::ET_TRIGGERPOINT:
			dst.type = FX_TRIGGERPOINT;
			dst.col = { 255, 0, 0, 255 };
			dst.triggerPoint.index = src->attr.t.index;
			break;
		case gta::ET_COVERPOINT:
			dst.type = FX_COVERPOINT;
			dst.col = { 0, 192, 255, 255 };
			dst.coverPoint.dirX = src->attr.c.dirOfCoverX;
			dst.coverPoint.dirY = src->attr.c.dirOfCoverY;
			dst.coverPoint.usage = src->attr.c.usage;
			break;
		case gta::ET_ESCALATOR:
			dst.type = FX_ESCALATOR;
			dst.col = { 255, 0, 255, 255 };
			if(localXform){
				rw::V3d::transformPoints(&dst.escalator.bottom, &src->attr.es.coords[0], 1, localXform);
				rw::V3d::transformPoints(&dst.escalator.top, &src->attr.es.coords[1], 1, localXform);
				rw::V3d::transformPoints(&dst.escalator.end, &src->attr.es.coords[2], 1, localXform);
			}else{
				dst.escalator.bottom = src->attr.es.coords[0];
				dst.escalator.top = src->attr.es.coords[1];
				dst.escalator.end = src->attr.es.coords[2];
			}
			dst.escalator.goingUp = src->attr.es.goingUp;
			break;
		default:
			continue;
		}

		Effects::AddEffect(dst);
	}
}

float
ObjectDef::GetLargestDrawDist(void)
{
	int i;
	float dd = 0.0f;
	for(i = 0; i < this->m_numAtomics; i++)
		if(this->m_drawDist[i] > dd)
			dd = this->m_drawDist[i];
	return dd;
}

rw::Atomic*
ObjectDef::GetAtomicForDist(float dist)
{
	// TODO: handle damaged atomics
	int i;
	for(i = 0; i < this->m_numAtomics; i++)
		if(dist < this->m_drawDist[i]*TheCamera.m_LODmult)
			return this->m_atomics[i];
	// We never want to return nil, so just pick one for the largest distance
	int n = 0;
	float dd = 0.0f;
	for(i = 0; i < this->m_numAtomics; i++)
		if(this->m_drawDist[i] > dd){
			dd = this->m_drawDist[i];
			n = i;
		}
	return this->m_atomics[n];
}

bool
ObjectDef::IsLoaded(void)
{
	if(m_type == ATOMIC)
		return m_atomics[0] != nil;
	else if(m_type == CLUMP)
		return m_clump != nil;
	return false;	// can't happen
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

static void
GetNameAndDamage(char *nodename, char *name, int *n)
{
	int len;
	len = strlen(nodename);
	if(strcmp(&nodename[len-4], "_dam") == 0){
		*n = 1;
		strncpy(name, nodename, len-4);
		name[len-4] = '\0';
	}else if(strcmp(&nodename[len-3], "_l0") == 0 ||
	         strcmp(&nodename[len-3], "_L0") == 0){
		*n = 0;
		strncpy(name, nodename, len-3);
		name[len-3] = '\0';
	}else{
		*n = 0;
		strncpy(name, nodename, len);
		name[len] = '\0';
	}
}

static void
SetupAtomic(rw::Atomic *atm)
{
	// Make sure we are not pre-instanced
	gta::attachCustomPipelines(atm);	// attach xbox pipelines, which we want to uninstance
	int32 driver = rw::platform;
	int32 platform = rw::findPlatform(atm);
	if(platform){
		rw::platform = platform;
		rw::switchPipes(atm, rw::platform);
	}
	if(atm->geometry->flags & rw::Geometry::NATIVE)
		atm->uninstance();
	rw::ps2::unconvertADC(atm->geometry);
	rw::platform = driver;
	// no need to switch back pipes because we reset it anyway

//	rw::MatFX::disableEffects(atm);	// so cloning won't reattach any MatFX pipes

	if(params.neoWorldPipe)
		atm->pipeline = neoWorldPipe;
	else if(params.leedsPipe)
		atm->pipeline = gta::leedsPipe;
	else if(params.daynightPipe && IsBuildingPipeAttached(atm))
		SetupBuildingPipe(atm);
	else{
		if(params.daynightPipe)
			// TEMPORARY because our MatFX can't do UV anim yet
			SetupBuildingPipe(atm);
		else
			atm->pipeline = nil;
	}
	atm->setRenderCB(myRenderCB);
}

void
ObjectDef::CantLoad(void)
{
	log("Can't load object %s\n", m_name);
	m_cantLoad = true;
}

static rw::Clump*
loadclump(rw::Stream *stream)
{
	using namespace rw;
	rw::Clump *c = nil;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(stream, &header);
	UVAnimDictionary *prev = currentUVAnimDictionary;
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(stream);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(stream, &header);
	}
	if(header.type == ID_CLUMP)
		c = Clump::streamRead(stream);
	currentUVAnimDictionary= prev;
	return c;
}

void
ObjectDef::LoadAtomic(void)
{
	uint8 *buffer;
	int size;
	bool looseFile = false;
	rw::StreamMemory stream;
	rw::Clump *clump;
	rw::Atomic *atomic;
	char *nodename;
	char name[MODELNAMELEN];
	int n;
	bool populate2dfx = m_effectIndex < 0 && m_numEffects == 0;

	const char *loosePath = ModloaderFindOverride(this->m_name, "dff");
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		looseFile = true;
	}else{
		buffer = ReadFileFromImage(this->m_imageIndex, &size);
	}
	stream.open((uint8*)buffer, size);
	clump = loadclump(&stream);
	if(clump){
		rw::Matrix invRoot;
		clump->getFrame()->updateObjects();
		rw::Matrix::invert(&invRoot, clump->getFrame()->getLTM());
		FORLIST(lnk, clump->atomics){
			rw::Matrix effectLocal;
			atomic = rw::Atomic::fromClump(lnk);
			rw::Matrix::mult(&effectLocal, atomic->getFrame()->getLTM(), &invRoot);
			if(populate2dfx)
				appendGeometry2dEffects(this, atomic, &effectLocal);
			nodename = gta::getNodeName(atomic->getFrame());
				if(!isSA())
					GetNameAndLOD(nodename, name, &n);
			else
				GetNameAndDamage(nodename, name, &n);
			SetAtomic(n, atomic);
			atomic->clump->removeAtomic(atomic);
			atomic->setFrame(rw::Frame::create());
			SetupAtomic(atomic);
		}
		clump->destroy();
		if(m_atomics[0] == nil)
			CantLoad();
	}
	stream.close();
	if(looseFile) free(buffer);
}

void
ObjectDef::LoadClump(void)
{
	uint8 *buffer;
	int size;
	bool looseFile = false;
	rw::StreamMemory stream;
	rw::Clump *clump;
	rw::Atomic *atomic;
	bool populate2dfx = m_effectIndex < 0 && m_numEffects == 0;

	const char *loosePath = ModloaderFindOverride(this->m_name, "dff");
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		looseFile = true;
	}else{
		buffer = ReadFileFromImage(this->m_imageIndex, &size);
	}
	stream.open((uint8*)buffer, size);
	clump = loadclump(&stream);
	if(clump){
		rw::Matrix invRoot;
		clump->getFrame()->updateObjects();
		rw::Matrix::invert(&invRoot, clump->getFrame()->getLTM());
		FORLIST(lnk, clump->atomics){
			rw::Matrix effectLocal;
			atomic = rw::Atomic::fromClump(lnk);
			if(populate2dfx){
				rw::Matrix::mult(&effectLocal, atomic->getFrame()->getLTM(), &invRoot);
				appendGeometry2dEffects(this, atomic, &effectLocal);
			}
			SetupAtomic(atomic);
		}
		SetClump(clump);
		if(m_clump == nil)
			CantLoad();
	}
	stream.close();
	if(looseFile) free(buffer);
}

void
ObjectDef::Load(void)
{
	if(m_cantLoad)
		return;

	bool hasLooseDff = ModloaderFindOverride(this->m_name, "dff") != nil;

	if(this->m_imageIndex < 0 && !hasLooseDff){
		log("warning: no streaming info for object %s %d %X\n", this->m_name, this->m_id, this->m_imageIndex);
		CantLoad();
		return;
	}
	if(this->m_txdSlot >= 0 && !IsTxdLoaded(this->m_txdSlot))
		LoadTxd(this->m_txdSlot);
	TxdMakeCurrent(this->m_txdSlot);

	SetTxdLookupContext(this->m_name, this->m_txdSlot);
	if(m_type == ATOMIC)
		LoadAtomic();
	else if(m_type == CLUMP)
		LoadClump();
	SetTxdLookupContext(nil, -1);
}

void
ObjectDef::SetAtomic(int n, rw::Atomic *atomic)
{
	if(this->m_atomics[n])
		log("warning: object %s already has atomic %d\n", m_name, n);
	this->m_atomics[n] = atomic;
	// TODO: set lighting
}

void
ObjectDef::SetClump(rw::Clump *clump)
{
	if(this->m_clump)
		log("warning: object %s already has clump\n", m_name);
	this->m_clump = clump;
	// TODO: set lighting
}

static ObjectDef*
FindRelatedObject(ObjectDef *obj, int first, int last)
{
	ObjectDef *obj2;
	int i;
	for(i = first; i < last; i++){
		obj2 = objdefs[i];
		if(obj2 && obj2 != obj &&
		   rw::strncmp_ci(obj->m_name+3, obj2->m_name+3, MODELNAMELEN) == 0)
			return obj2;
	}
	return nil;
}

void
ObjectDef::SetupBigBuilding(int first, int last)
{
	ObjectDef *hqobj;
	if(m_drawDist[0] > LODDISTANCE)
		m_isBigBuilding = true;

	// in SA level of detail is handled by instances
	if(!isSA() && m_isBigBuilding && m_relatedModel == nil){
		hqobj = FindRelatedObject(this, first, last);
		if(hqobj){
			hqobj->m_relatedModel = this;
			m_relatedModel = hqobj;
			m_minDrawDist = hqobj->GetLargestDrawDist();
		}else
			m_minDrawDist = params.map == GAME_III ? 100.0f : 0.0f;
	}
}

void
ObjectDef::SetFlags(int flags)
{
	switch(params.objFlagset){
	case GAME_III:
		if(flags & 1) m_normalCull = true;
		if(flags & 2) m_noFade = true;
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x10) m_isSubway = true;
		if(flags & 0x20) m_ignoreLight = true;
		if(flags & 0x40) m_noZwrite = true;
		break;
	case GAME_VC:
		if(flags & 1) m_wetRoadReflection = true;
		if(flags & 2) m_noFade = true;
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x10) m_isSubway = true;	// probably used
		if(flags & 0x20) m_ignoreLight = true;
		if(flags & 0x40) m_noZwrite = true;
		if(flags & 0x80) m_noShadows = true;
		if(flags & 0x100) m_ignoreDrawDist = true;
		if(flags & 0x200) m_isCodeGlass = true;
		if(flags & 0x400) m_isArtistGlass = true;
		break;
	case GAME_SA:
		// base model info
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x40) m_noZwrite = true;
		if(flags & 0x80) m_noShadows = true;
		if(flags & 0x200000) m_noBackfaceCulling = true;

		if(m_type == ATOMIC){
			if(flags & 1) m_wetRoadReflection = true;
			if(flags & 0x8000) m_dontCollideWithFlyer = true;
			// these are exclusive:
			if(flags &    0x200) m_isCodeGlass = true;
			if(flags &    0x400) m_isArtistGlass = true;
			if(flags &    0x800) m_isGarageDoor = true;
			if(!m_isTimed)
				if(flags & 0x1000) m_isDamageable = true;
			if(flags &   0x2000) m_isTree = true;
			if(flags &   0x4000) m_isPalmTree = true;
			if(flags & 0x100000) m_isTag = true;
			if(flags & 0x400000) m_noCover = true;
			if(flags & 0x800000) m_wetOnly = true;

//			if(flags & ~0xF0FECD)
//				printf("Object has unknown flags: %s %x %x\n", m_name, flags, flags&~0xF0FECD);
		}else if(m_type == CLUMP){
			if(flags & 0x20) m_isDoor = true;
		}
		break;
	}
}

ObjectDef*
AddObjectDef(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS){
		log("warning: object id %d out of range (max %d), skipping\n", id, NUMOBJECTDEFS-1);
		return nil;
	}
	ObjectDef *obj = new ObjectDef;
	memset(obj, 0, sizeof(ObjectDef));
	obj->m_imageIndex = -1;
	obj->m_pedPathIndex = -1;
	obj->m_carPathIndex = -1;
	obj->m_effectIndex = -1;
	obj->m_numEffects = 0;
	if(objdefs[id])
		log("warning: id %d already defined as %s %d\n", id, objdefs[id]->m_name, objdefs[id]->m_id);
	objdefs[id] = obj;
	obj->m_id = id;
	return obj;
}

void
RemoveObjectDef(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS)
		return;
	ObjectDef *obj = objdefs[id];
	if(obj == nil)
		return;

	for(int i = 0; i < nelem(objdefs); i++){
		ObjectDef *other = objdefs[i];
		if(other == nil || other == obj)
			continue;
		if(other->m_relatedModel == obj)
			other->m_relatedModel = nil;
		if(other->m_relatedTimeModel == obj)
			other->m_relatedTimeModel = nil;
	}

	if(obj->m_clump){
		obj->m_clump->destroy();
		obj->m_clump = nil;
	}else{
		for(int i = 0; i < nelem(obj->m_atomics); i++){
			if(obj->m_atomics[i]){
				destroyDetachedAtomic(obj->m_atomics[i]);
				obj->m_atomics[i] = nil;
			}
		}
	}

	if(obj->m_colModel){
		delete obj->m_colModel;
		obj->m_colModel = nil;
	}

	delete obj;
	objdefs[id] = nil;
}

ObjectDef*
GetObjectDef(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return nil;
	return objdefs[id];
}

ObjectDef*
GetObjectDef(const char *name, int *id)
{
	int i;
	for(i = 0; i < nelem(objdefs); i++){
		if(objdefs[i] == nil)
			continue;
		if(rw::strncmp_ci(objdefs[i]->m_name, name, MODELNAMELEN) == 0){
			if(id)
				*id = i;
			return objdefs[i];
		}
	}
	return nil;
}
