#include "euryopa.h"
#include "lod_associations.h"
#include "object_categories.h"

CPtrList instances;
CPtrList selection;

static int
countLiveLodChildren(ObjectInst *lodInst, ObjectInst *ignoreInst)
{
	int count = 0;
	CPtrNode *p;

	if(lodInst == nil)
		return 0;

	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other == ignoreInst || other->m_isDeleted)
			continue;
		if(other->m_lod == lodInst)
			count++;
	}
	return count;
}

void
ObjectInst::UpdateMatrix(void)
{
	m_matrix.rotate(conj(m_rotation), rw::COMBINEREPLACE);
	m_matrix.translate(&m_translation, rw::COMBINEPOSTCONCAT);
}

void*
ObjectInst::CreateRwObject(void)
{
	rw::Frame *f;
	rw::Atomic *atomic;
	rw::Clump *clump;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil) return nil;

	if(obj->m_type == ObjectDef::ATOMIC){
		if(obj->m_atomics[0] == nil)
			return nil;
		atomic = obj->m_atomics[0]->clone();
		f = rw::Frame::create();
		atomic->setFrame(f);
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = atomic;
	}else if(obj->m_type == ObjectDef::CLUMP){
		if(obj->m_clump == nil)
			return nil;
		clump = obj->m_clump->clone();
		f = clump->getFrame();
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = clump;
	}
	return m_rwObject;
}

void
ObjectInst::Init(FileObjectInstance *fi)
{
	m_objectId = fi->objectId;
	if(fi->area & 0x100) m_isUnimportant = true;
	if(fi->area & 0x400) m_isUnderWater = true;
	if(fi->area & 0x800) m_isTunnel = true;
	if(fi->area & 0x1000) m_isTunnelTransition = true;
	m_area = fi->area & 0xFF;
	m_rotation = fi->rotation;
	m_translation = fi->position;
	m_origTranslation = fi->position;
	m_origRotation = fi->rotation;
	m_savedTranslation = fi->position;
	m_savedRotation = fi->rotation;
	m_savedStateValid = true;
	m_wasSavedDeleted = false;
	m_lodId = fi->lod;
	UpdateMatrix();
}

void
ObjectInst::SetupBigBuilding(void)
{
	m_isBigBuilding = true;
}

CRect
ObjectInst::GetBoundRect(void)
{
	CRect rect;
	rw::V3d v;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return rect;
	CColModel *col = obj->m_colModel;

	for(int ix = 0; ix < 2; ix++)
	for(int iy = 0; iy < 2; iy++)
	for(int iz = 0; iz < 2; iz++){
		v.x = ix ? col->boundingBox.max.x : col->boundingBox.min.x;
		v.y = iy ? col->boundingBox.max.y : col->boundingBox.min.y;
		v.z = iz ? col->boundingBox.max.z : col->boundingBox.min.z;
		rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
		rect.ContainPoint(v);
	}

	return rect;
}

bool
ObjectInst::IsOnScreen(void)
{
	rw::Sphere sph;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil)
		return false;
	if(obj->m_colModel == nil)
		return true;
	CColModel *col = obj->m_colModel;
	sph.center = col->boundingSphere.center;
	sph.radius = col->boundingSphere.radius;
	return TheCamera.IsSphereVisible(&sph, &m_matrix);
}

static void
updateMatFXAnim(rw::Material *m)
{
	if(!rw::UVAnim::exists(m))
		return;
	rw::UVAnim::addTime(m, timeStep);
	rw::UVAnim::applyUpdate(m);
}

void
ObjectInst::PreRender(void)
{
	int i;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil)
		return;
	if(obj->m_hasPreRendered)
		return;
	obj->m_hasPreRendered = true;

	if(obj->m_type == ObjectDef::ATOMIC && gPlayAnimations){
		rw::Atomic *atm = (rw::Atomic*)m_rwObject;
		if(rw::MatFX::getEffects(atm))
			for(i = 0; i < atm->geometry->matList.numMaterials; i++)
				updateMatFXAnim(atm->geometry->matList.materials[i]);
	}
}

void
ObjectInst::JumpTo(void)
{
	rw::V3d center;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return;
	CSphere *sph = &obj->m_colModel->boundingSphere;
	rw::V3d::transformPoints(&center, &sph->center, 1, &m_matrix);
	TheCamera.setTarget(center);
	TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(sph->radius));
}

void
ObjectInst::Select(void)
{
	if(m_selected) return;
	m_selected = true;
	selection.InsertItem(this);
}

void
ObjectInst::Deselect(void)
{
	CPtrNode *p;
	if(!m_selected) return;
	m_selected = false;
	for(p = selection.first; p; p = p->next)
		if(p->item == this){
			selection.RemoveNode(p);
			return;
		}
}

void
ClearSelection(void)
{
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		((ObjectInst*)p->item)->m_selected = false;
		selection.RemoveNode(p);
	}
}

void
ObjectInst::Delete(void)
{
	if(m_isDeleted) return;
	m_isDeleted = true;
	StampChangeSeq(this);
	Deselect();

	// If this HD building was the last live child of its LOD, delete the LOD too.
	if(m_lod && !m_lod->m_isDeleted &&
	   countLiveLodChildren(m_lod, this) == 0)
		m_lod->Delete();

	// If this IS a LOD, delete all HD buildings that reference it
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && !other->m_isDeleted)
			other->Delete();
	}
}

void
ObjectInst::Undelete(void)
{
	if(!m_isDeleted) return;
	m_isDeleted = false;
	StampChangeSeq(this);
	if(m_imageIndex >= 0 && m_wasSavedDeleted)
		m_isDirty = true;

	// Also undelete the LOD pair
	if(m_lod && m_lod->m_isDeleted)
		m_lod->Undelete();

	// If this IS a LOD, undelete HD children
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && other->m_isDeleted)
			other->Undelete();
	}
}

void
DeleteSelected(void)
{
	// Collect all instances that will be deleted (including LOD cascades)
	// First, gather the selected ones
	ObjectInst *toDelete[64];
	int numToDelete = 0;
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && numToDelete < 64)
			toDelete[numToDelete++] = inst;
	}
	if(numToDelete == 0) return;

	// Now delete them and collect ALL that got deleted (including LOD cascade)
	ObjectInst *allDeleted[64];
	int numAllDeleted = 0;

	// Snapshot which are already deleted
	// Then delete, and find newly deleted ones
	for(int i = 0; i < numToDelete; i++)
		toDelete[i]->Delete();

	// Scan for all instances that are now deleted to record in undo
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted && numAllDeleted < 64){
			// Check if this was freshly deleted (it's in our set or cascaded)
			// Simple approach: just record all deleted. On undo we undelete them.
			// This is slightly broad but safe.
		}
	}

	// Simpler: just record what we explicitly asked to delete,
	// the cascade is handled by Undelete() automatically
	UndoRecordDelete(toDelete, numToDelete);
}

// Object Spawner state
bool gPlaceMode;
static int spawnObjectId = -1;
static GameFile *customIplFile = nil;
static const char *CUSTOM_IPL_PATH = "data\\maps\\custom.ipl";

#define LOD_LOOKUP_SIZE 20000
static int lodLookup[LOD_LOOKUP_SIZE];

void
InitLodLookup(void)
{
	memset(lodLookup, -1, sizeof(lodLookup));
	if(!isSA()) return;
	for(int i = 0; saLodAssociations[i][0] >= 0; i++){
		int hd = saLodAssociations[i][0];
		int lod = saLodAssociations[i][1];
		if(hd < LOD_LOOKUP_SIZE)
			lodLookup[hd] = lod;
	}
}

int
GetSpawnObjectId(void)
{
	return spawnObjectId;
}

void
SetSpawnObjectId(int id)
{
	spawnObjectId = id;
}

int
GetLodForObject(int id)
{
	if(isSA()){
		if(id >= 0 && id < LOD_LOOKUP_SIZE)
			return lodLookup[id];
		return -1;
	}else{
		ObjectDef *obj = GetObjectDef(id);
		if(obj && obj->m_relatedModel && !obj->m_isBigBuilding)
			return obj->m_relatedModel->m_id;
		return -1;
	}
}

static const char*
GetDatFilename(void)
{
	switch(gameversion){
	case GAME_III: return "data/gta3.dat";
	case GAME_VC:  return "data/gta_vc.dat";
	case GAME_SA:  return "data/gta.dat";
	case GAME_LCS: return "data/gta_lcs.dat";
	case GAME_VCS: return "data/gta_vcs.dat";
	default:       return nil;
	}
}

static bool
IplEntryExistsInDat(const char *datfile, const char *iplpath)
{
	FILE *f = fopen_ci(datfile, "r");
	if(f == nil) return false;
	char line[512];
	while(fgets(line, sizeof(line), f)){
		// strip newline
		char *p = line;
		while(*p && *p != '\r' && *p != '\n') p++;
		*p = '\0';
		if(strncmp(line, "IPL ", 4) == 0){
			if(strcmp(line+4, iplpath) == 0){
				fclose(f);
				return true;
			}
		}
	}
	fclose(f);
	return false;
}

static void
AppendIplToDat(const char *iplpath)
{
	const char *datfile = GetDatFilename();
	if(datfile == nil) return;
	if(IplEntryExistsInDat(datfile, iplpath)) return;

	char *ospath = getPath(datfile);
	FILE *f = fopen(ospath, "a");
	if(f == nil){
		log("WARNING: could not append to %s\n", datfile);
		return;
	}
	fprintf(f, "\nIPL %s\n", iplpath);
	fclose(f);
	log("Added IPL %s to %s\n", iplpath, datfile);
}

static GameFile*
GetOrCreateCustomIplFile(void)
{
	if(customIplFile)
		return customIplFile;
	char path[256];
	strncpy(path, CUSTOM_IPL_PATH, sizeof(path));
	customIplFile = NewGameFile(path);
	AppendIplToDat(CUSTOM_IPL_PATH);
	return customIplFile;
}

static int
GetMaxIplIndexForFile(GameFile *file)
{
	int maxIplIndex = -1;
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_file == file && other->m_imageIndex < 0){
			if(other->m_iplIndex > maxIplIndex)
				maxIplIndex = other->m_iplIndex;
		}
	}
	return maxIplIndex;
}

static ObjectInst*
createSpawnedInstance(int objectId, rw::V3d position, GameFile *file, int iplIndex)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = objectId;
	inst->m_area = currentArea;
	inst->m_rotation.x = 0;
	inst->m_rotation.y = 0;
	inst->m_rotation.z = 0;
	inst->m_rotation.w = 1;
	inst->m_translation = position;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = file;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = iplIndex;
	inst->m_isAdded = true;
	inst->m_isDirty = true;
	inst->m_savedStateValid = false;
	inst->m_wasSavedDeleted = false;
	StampChangeSeq(inst);

	ObjectDef *obj = GetObjectDef(objectId);

	if(obj && obj->m_isBigBuilding)
		inst->SetupBigBuilding();

	inst->UpdateMatrix();

	if(obj && !obj->IsLoaded()){
		RequestObject(objectId);
		LoadAllRequestedObjects();
	}

	inst->CreateRwObject();

	if(obj && obj->m_colModel)
		InsertInstIntoSectors(inst);
	else{
		CPtrList *list = inst->m_isBigBuilding
			? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
	}

	return inst;
}

static void
finalizeLinkedLod(ObjectInst *hdInst, ObjectInst *lodInst)
{
	ObjectDef *hdObj, *lodObj;

	hdInst->m_lodId = lodInst->m_iplIndex;
	hdInst->m_lod = lodInst;
	lodInst->m_numChildren = 1;

	if(!lodInst->m_isBigBuilding)
		lodInst->SetupBigBuilding();

	hdObj = GetObjectDef(hdInst->m_objectId);
	lodObj = GetObjectDef(lodInst->m_objectId);
	if(hdObj && lodObj && lodInst->m_numChildren == 1 && hdObj->m_colModel){
		lodObj->m_colModel = hdObj->m_colModel;
		lodObj->m_gotChildCol = true;
	}

	RemoveInstFromSectors(lodInst);
	InsertInstIntoSectors(lodInst);
}

void
SpawnPlaceObject(rw::V3d position)
{
	if(spawnObjectId < 0) return;
	ObjectDef *obj = GetObjectDef(spawnObjectId);
	if(obj == nil) return;

	GameFile *file = GetOrCreateCustomIplFile();
	int maxIdx = GetMaxIplIndexForFile(file);

	ObjectInst *pasted[4];
	int numPasted = 0;

	// Resolve LOD object
	int lodObjId = -1;
	if(isSA()){
		if(spawnObjectId < LOD_LOOKUP_SIZE)
			lodObjId = lodLookup[spawnObjectId];
	}else{
		if(obj->m_relatedModel && !obj->m_isBigBuilding)
			lodObjId = obj->m_relatedModel->m_id;
	}

	// Create LOD first (if exists)
	ObjectInst *lodInst = nil;
	if(lodObjId >= 0 && GetObjectDef(lodObjId)){
		lodInst = createSpawnedInstance(lodObjId, position, file, ++maxIdx);
		pasted[numPasted++] = lodInst;
	}

	// Create HD instance
	ObjectInst *hdInst = createSpawnedInstance(spawnObjectId, position, file, ++maxIdx);
	if(lodInst)
		finalizeLinkedLod(hdInst, lodInst);

	ClearSelection();
	hdInst->Select();
	pasted[numPasted++] = hdInst;

	UndoRecordPaste(pasted, numPasted);
	Toast(TOAST_SPAWN, "Placed %s", obj->m_name);
}

void
SpawnExitPlaceMode(void)
{
	gPlaceMode = false;
	spawnObjectId = -1;
}

// Object Browser categories
static int categoryLookup[NUMOBJECTDEFS];

void
InitObjectCategories(void)
{
	memset(categoryLookup, -1, sizeof(categoryLookup));
	for(int i = 0; objCategoryMap[i][0] >= 0; i++){
		int id = objCategoryMap[i][0];
		int cat = objCategoryMap[i][1];
		if(id < NUMOBJECTDEFS && categoryLookup[id] < 0)
			categoryLookup[id] = cat;
	}
}

int
GetObjectCategory(int modelId)
{
	if(modelId < 0 || modelId >= NUMOBJECTDEFS) return -1;
	return categoryLookup[modelId];
}

// Favourites
static bool favourites[NUMOBJECTDEFS];

void
LoadFavourites(void)
{
	memset(favourites, 0, sizeof(favourites));
	FILE *f = fopen("favourites.txt", "r");
	if(f == nil) return;
	char line[64];
	while(fgets(line, sizeof(line), f)){
		int id = atoi(line);
		if(id >= 0 && id < NUMOBJECTDEFS)
			favourites[id] = true;
	}
	fclose(f);
}

void
SaveFavourites(void)
{
	FILE *f = fopen("favourites.txt", "w");
	if(f == nil) return;
	for(int i = 0; i < NUMOBJECTDEFS; i++)
		if(favourites[i])
			fprintf(f, "%d\n", i);
	fclose(f);
}

bool
IsFavourite(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return false;
	return favourites[id];
}

void
ToggleFavourite(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return;
	favourites[id] = !favourites[id];
	SaveFavourites();
}

// Diff viewer — monotonic change counter for chronological ordering
static uint32 gChangeSeqCounter = 0;

void
StampChangeSeq(ObjectInst *inst)
{
	inst->m_changeSeq = ++gChangeSeqCounter;
}

// Diff viewer — compute bitmask of changes since last save
int
GetInstanceDiffFlags(ObjectInst *inst)
{
	if(!inst->m_savedStateValid){
		// Never saved — it's new (if not deleted)
		if(!inst->m_isDeleted)
			return DIFF_ADDED;
		return 0;	// created then deleted before save — ignore
	}
	if(inst->m_isDeleted && !inst->m_wasSavedDeleted)
		return DIFF_DELETED;
	if(inst->m_isDeleted)
		return 0;	// was deleted at save, still deleted

	int flags = 0;
	if(!inst->m_isDeleted && inst->m_wasSavedDeleted)
		flags |= DIFF_RESTORED;
	float dist = length(sub(inst->m_translation, inst->m_savedTranslation));
	if(dist >= 0.001f)
		flags |= DIFF_MOVED;
	// quaternion distance: if |dot| < 0.9999 the rotation changed
	float dot = fabsf(inst->m_rotation.x * inst->m_savedRotation.x +
	                   inst->m_rotation.y * inst->m_savedRotation.y +
	                   inst->m_rotation.z * inst->m_savedRotation.z +
	                   inst->m_rotation.w * inst->m_savedRotation.w);
	if(dot < 0.9999f)
		flags |= DIFF_ROTATED;
	return flags;
}

// 3D Preview — uses Scene camera with swapped framebuffers (librw pattern)
rw::Texture *gPreviewTexture;
static rw::Raster *previewColorRaster;
static rw::Raster *previewDepthRaster;
static bool previewInited;
static bool previewInitFailed;

#define PREVIEW_SIZE 256

void
InitPreviewRenderer(void)
{
	if(previewInited || previewInitFailed) return;

	previewColorRaster = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 0, rw::Raster::CAMERATEXTURE);
	if(previewColorRaster == nil){ previewInitFailed = true; return; }
	previewDepthRaster = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 0, rw::Raster::ZBUFFER);
	if(previewDepthRaster == nil){ previewInitFailed = true; return; }
	gPreviewTexture = rw::Texture::create(previewColorRaster);
	gPreviewTexture->setFilter(rw::Texture::LINEAR);
	previewInited = true;
}

void
ShutdownPreviewRenderer(void)
{
	if(gPreviewTexture){
		gPreviewTexture->raster = nil;
		gPreviewTexture->destroy();
		gPreviewTexture = nil;
	}
	if(previewColorRaster){ previewColorRaster->destroy(); previewColorRaster = nil; }
	if(previewDepthRaster){ previewDepthRaster->destroy(); previewDepthRaster = nil; }
	previewInited = false;
}

void
RenderPreviewObject(int objectId)
{
	if(previewInitFailed) return;
	if(objectId < 0) return;
	if(!previewInited) InitPreviewRenderer();
	if(!previewInited) return;

	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil) return;
	if(!obj->IsLoaded()){
		RequestObject(objectId);
		return;
	}

	// Clone model for preview (persistent, rebuilt when selection changes)
	static rw::Atomic *previewAtm = nil;
	static rw::Clump *previewClump = nil;
	static int previewCloneId = -1;

	if(previewCloneId != objectId){
		if(previewAtm){ previewAtm->getFrame()->destroy(); previewAtm->destroy(); previewAtm = nil; }
		if(previewClump){ previewClump->getFrame()->destroy(); previewClump->destroy(); previewClump = nil; }
		previewCloneId = objectId;

		if(obj->m_type == ObjectDef::ATOMIC && obj->m_atomics[0]){
			previewAtm = obj->m_atomics[0]->clone();
			rw::Frame *f = rw::Frame::create();
			previewAtm->setFrame(f);
		}else if(obj->m_type == ObjectDef::CLUMP && obj->m_clump){
			previewClump = obj->m_clump->clone();
		}
	}
	if(previewAtm == nil && previewClump == nil) return;

	// Camera setup
	float radius = 5.0f;
	if(obj->m_colModel)
		radius = obj->m_colModel->boundingSphere.radius;
	if(radius < 1.0f) radius = 1.0f;
	float dist = radius * 2.5f;

	static float angle = 0.0f;
	angle += 0.01f;
	if(angle > 2.0f * 3.14159f) angle -= 2.0f * 3.14159f;

	rw::V3d target = { 0.0f, 0.0f, 0.0f };
	if(obj->m_colModel)
		target = obj->m_colModel->boundingSphere.center;
	rw::V3d eye = { target.x + dist*cosf(angle), target.y + dist*sinf(angle), target.z + dist*0.4f };
	rw::V3d up = { 0.0f, 0.0f, -1.0f };  // negative Z because FBO is Y-flipped
	rw::V3d dir = normalize(sub(target, eye));

	// Use scene camera with swapped framebuffers (librw pattern)
	rw::Camera *cam = Scene.camera;

	// Save ALL camera state
	rw::Raster *savedFB = cam->frameBuffer;
	rw::Raster *savedZB = cam->zBuffer;
	float savedNear = cam->nearPlane;
	float savedFar = cam->farPlane;
	float savedFog = cam->fogPlane;
	rw::Frame *camFrame = cam->getFrame();
	rw::Matrix savedMatrix = camFrame->matrix;

	// Set preview camera state
	cam->frameBuffer = previewColorRaster;
	cam->zBuffer = previewDepthRaster;
	cam->setNearPlane(0.1f);
	cam->setFarPlane(1000.0f);
	cam->fogPlane = 900.0f;
	cam->setFOV(60.0f, 1.0f);
	camFrame->matrix.pos = eye;
	camFrame->matrix.lookAt(dir, up);
	camFrame->updateObjects();

	static rw::RGBA clearCol = { 40, 40, 40, 255 };
	cam->clear(&clearCol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	cam->beginUpdate();

	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	pAmbient->setColor(0.7f, 0.7f, 0.7f);
	pDirect->setColor(0.5f, 0.5f, 0.5f);

	rw::Matrix ident;
	ident.setIdentity();
	if(previewAtm){
		previewAtm->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		previewAtm->render();
	}else if(previewClump){
		previewClump->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		previewClump->render();
	}

	cam->endUpdate();

	// Restore ALL camera state
	cam->frameBuffer = savedFB;
	cam->zBuffer = savedZB;
	cam->setNearPlane(savedNear);
	cam->setFarPlane(savedFar);
	cam->fogPlane = savedFog;
	cam->setFOV(TheCamera.m_fov, TheCamera.m_aspectRatio);
	camFrame->matrix = savedMatrix;
	camFrame->updateObjects();

	Timecycle::SetLights();
}

// Clipboard
#define MAX_CLIPBOARD 64
static ObjectInst *clipboard[MAX_CLIPBOARD];
static int clipboardCount;

// Undo/Redo system
#define MAX_UNDO 128
static UndoAction undoStack[MAX_UNDO];
static int undoCount;	// number of actions in stack
static int undoPos;	// current position (next undo)

static void
updateRwFrameForInst(ObjectInst *inst)
{
	if(inst->m_rwObject == nil) return;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;
	rw::Frame *f;
	if(obj->m_type == ObjectDef::ATOMIC)
		f = ((rw::Atomic*)inst->m_rwObject)->getFrame();
	else
		f = ((rw::Clump*)inst->m_rwObject)->getFrame();
	f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
}

static void
applyUndoTransform(const UndoTransform &t, bool useNewState)
{
	ObjectInst *inst = t.inst;
	if(inst == nil)
		return;

	bool refreshSectors = (t.flags & (UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT)) != 0;
	if(refreshSectors)
		RemoveInstFromSectors(inst);
	if(t.flags & UNDO_TRANSFORM_POS)
		inst->m_translation = useNewState ? t.newPos : t.oldPos;
	if(t.flags & UNDO_TRANSFORM_ROT)
		inst->m_rotation = useNewState ? t.newRot : t.oldRot;

	inst->m_isDirty = true;
	inst->UpdateMatrix();
	updateRwFrameForInst(inst);

	if(refreshSectors)
		InsertInstIntoSectors(inst);
}

static void
pushUndo(UndoAction *a)
{
	// If we're not at the top, discard redo history
	undoCount = undoPos;
	if(undoCount >= MAX_UNDO){
		// Shift everything down
		memmove(&undoStack[0], &undoStack[1], (MAX_UNDO-1)*sizeof(UndoAction));
		undoCount--;
		undoPos--;
	}
	undoStack[undoCount] = *a;
	undoCount++;
	undoPos = undoCount;
}

void
UndoRecordMove(ObjectInst *inst, rw::V3d oldPos, ObjectInst *lodInst, rw::V3d lodOldPos)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_MOVE;
	a.inst = inst;
	a.oldPos = oldPos;
	a.newPos = inst->m_translation;
	a.lodInst = lodInst;
	a.lodOldPos = lodOldPos;
	if(lodInst)
		a.lodNewPos = lodInst->m_translation;
	pushUndo(&a);
}

void
UndoRecordRotate(ObjectInst *inst, rw::Quat oldRot)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_ROTATE;
	a.inst = inst;
	a.oldRot = oldRot;
	a.newRot = inst->m_rotation;
	pushUndo(&a);
}

void
UndoRecordDelete(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_DELETE;
	a.numDeleted = num > 64 ? 64 : num;
	for(int i = 0; i < a.numDeleted; i++)
		a.deletedInsts[i] = insts[i];
	pushUndo(&a);
}

void
UndoRecordPaste(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_PASTE;
	a.numPasted = num > 64 ? 64 : num;
	for(int i = 0; i < a.numPasted; i++)
		a.pastedInsts[i] = insts[i];
	pushUndo(&a);
}

void
UndoRecordTransformBatch(UndoTransform *transforms, int num)
{
	if(num <= 0)
		return;

	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_TRANSFORM_BATCH;
	a.numTransforms = num > 64 ? 64 : num;
	for(int i = 0; i < a.numTransforms; i++)
		a.transforms[i] = transforms[i];
	pushUndo(&a);
}

void
Undo(void)
{
	if(undoPos <= 0) return;
	undoPos--;
	UndoAction *a = &undoStack[undoPos];

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->oldPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodOldPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->oldRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			a->deletedInsts[i]->Undelete();
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Deselect();
			a->pastedInsts[i]->Delete();
		}
		break;
	case UNDO_TRANSFORM_BATCH:
		for(int i = 0; i < a->numTransforms; i++)
			applyUndoTransform(a->transforms[i], false);
		break;
	}
}

void
Redo(void)
{
	if(undoPos >= undoCount) return;
	UndoAction *a = &undoStack[undoPos];
	undoPos++;

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->newPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodNewPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->newRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			a->deletedInsts[i]->Delete();
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Undelete();
			a->pastedInsts[i]->Select();
		}
		break;
	case UNDO_TRANSFORM_BATCH:
		for(int i = 0; i < a->numTransforms; i++)
			applyUndoTransform(a->transforms[i], true);
		break;
	}
}

ObjectInst*
GetInstanceByID(int32 id)
{
	CPtrNode *p;
	for(p = instances.first; p; p = p->next)
		if(((ObjectInst*)p->item)->m_id == id)
			return (ObjectInst*)p->item;
	return nil;
}

ObjectInst*
AddInstance(void)
{
	static int32 id = 1;

	ObjectInst *inst = new ObjectInst;
	memset(inst, 0, sizeof(ObjectInst));
	inst->m_id = id++;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	instances.InsertItem(inst);
	return inst;
}

void
CopySelected(void)
{
	clipboardCount = 0;
	CPtrNode *p;
	for(p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && clipboardCount < MAX_CLIPBOARD)
			clipboard[clipboardCount++] = inst;
	}
	if(clipboardCount > 0)
		log("Copied %d instance(s)\n", clipboardCount);
}

static ObjectInst*
cloneInstance(ObjectInst *src, GameFile *dstFile, int iplIndex, rw::V3d offset)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = src->m_objectId;
	inst->m_area = src->m_area;
	inst->m_rotation = src->m_rotation;
	inst->m_translation.x = src->m_translation.x + offset.x;
	inst->m_translation.y = src->m_translation.y + offset.y;
	inst->m_translation.z = src->m_translation.z + offset.z;
	inst->m_isUnimportant = src->m_isUnimportant;
	inst->m_isUnderWater = src->m_isUnderWater;
	inst->m_isTunnel = src->m_isTunnel;
	inst->m_isTunnelTransition = src->m_isTunnelTransition;
	inst->m_isBigBuilding = src->m_isBigBuilding;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = dstFile;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = iplIndex;
	inst->m_isAdded = true;
	inst->m_isDirty = true;
	inst->m_savedStateValid = false;
	inst->m_wasSavedDeleted = false;
	StampChangeSeq(inst);
	inst->UpdateMatrix();
	inst->CreateRwObject();
	InsertInstIntoSectors(inst);
	return inst;
}

void
PasteClipboard(void)
{
	if(clipboardCount == 0) return;

	rw::V3d offset = { 10.0f, 0.0f, 0.0f };

	ObjectInst *pasted[MAX_CLIPBOARD * 2];
	int numPasted = 0;

	// Deduplicate: skip LODs that are already the m_lod of another
	// clipboard entry (they'll be auto-copied with their HD parent)
	ObjectInst *toPaste[MAX_CLIPBOARD];
	int numToPaste = 0;

	for(int i = 0; i < clipboardCount; i++){
		bool isLodOfAnother = false;
		for(int j = 0; j < clipboardCount; j++){
			if(i != j && clipboard[j]->m_lod == clipboard[i]){
				isLodOfAnother = true;
				break;
			}
		}
		if(!isLodOfAnother)
			toPaste[numToPaste++] = clipboard[i];
	}

	ClearSelection();

	for(int i = 0; i < numToPaste; i++){
		ObjectInst *src = toPaste[i];
		ObjectInst *srcLod = src->m_lod;

		// Determine destination text IPL file.
		// For streaming instances, use the LOD's file (which is the text IPL).
		// If no LOD, scan for a text IPL instance from the same streaming base.
		GameFile *dstFile = nil;
		if(src->m_imageIndex < 0){
			// Source is text IPL — use same file
			dstFile = src->m_file;
		}else if(srcLod && srcLod->m_file){
			// Streaming HD — use its LOD's text IPL file
			dstFile = srcLod->m_file;
		}else{
			// Streaming HD with no LOD — find any text IPL instance
			// that shares this LOD's file as a fallback.
			// Last resort: just use src->m_file (will create standalone text IPL)
			dstFile = src->m_file;
		}

		// Find the max m_iplIndex for this file (text IPL instances only)
		int maxIplIndex = -1;
		CPtrNode *p;
		for(p = instances.first; p; p = p->next){
			ObjectInst *other = (ObjectInst*)p->item;
			if(other->m_file == dstFile && other->m_imageIndex < 0){
				if(other->m_iplIndex > maxIplIndex)
					maxIplIndex = other->m_iplIndex;
			}
		}

		// Paste LOD first (if any) so we have its iplIndex for the HD's lodId
		ObjectInst *newLod = nil;
		if(isSA() && srcLod && !srcLod->m_isDeleted){
			newLod = cloneInstance(srcLod, dstFile, ++maxIplIndex, offset);
			pasted[numPasted++] = newLod;
		}

		// Paste HD
		ObjectInst *newHd = cloneInstance(src, dstFile, ++maxIplIndex, offset);

		// Link LOD
		if(newLod)
			finalizeLinkedLod(newHd, newLod);

		newHd->Select();
		pasted[numPasted++] = newHd;
	}

	if(numPasted > 0){
		UndoRecordPaste(pasted, numPasted);
		log("Pasted %d instance(s)\n", numPasted);
	}
}





int numSectorsX, numSectorsY;
CRect worldBounds;
Sector *sectors;
Sector outOfBoundsSector;

void
InitSectors(void)
{
	switch(params.map){
	case GAME_III:
		numSectorsX = 100;
		numSectorsY = 100;
		worldBounds.left = -2000.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 2000.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_VC:
		numSectorsX = 80;
		numSectorsY = 80;
		worldBounds.left = -2400.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 1600.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_SA:
		numSectorsX = 120;
		numSectorsY = 120;
		worldBounds.left = -10000.0f;
		worldBounds.bottom = -10000.0f;
		worldBounds.right = 10000.0f;
		worldBounds.top = 10000.0f;
		break;
	}
	sectors = new Sector[numSectorsX*numSectorsY];
}

Sector*
GetSector(int ix, int iy)
{
	return &sectors[ix*numSectorsY + iy];
}

//Sector*
//GetSector(float x, float y)
//{
//	int i, j;
//	assert(x > worldBounds.left && x < worldBounds.right);
//	assert(y > worldBounds.bottom && y < worldBounds.top);
//	i = (x + worldBounds.right - worldBounds.left)/numSectorsX;
//	j = (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
//	return GetSector(i, j);
//}

int
GetSectorIndexX(float x)
{
	if(x < worldBounds.left) x = worldBounds.left;
	if(x >= worldBounds.right) x = worldBounds.right - 1.0f;
	return (x - worldBounds.left) / ((worldBounds.right - worldBounds.left) / numSectorsX);
}

int
GetSectorIndexY(float y)
{
	if(y < worldBounds.bottom) y = worldBounds.bottom;
	if(y >= worldBounds.top) y = worldBounds.top - 1.0f;
	return (y - worldBounds.bottom) / ((worldBounds.top - worldBounds.bottom) / numSectorsY);
}

bool
IsInstInBounds(ObjectInst *inst)
{
	ObjectDef *obj;
	// Some objects don't have collision data
	// TODO: figure out what to do with them
	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return false;
	CRect bounds = inst->GetBoundRect();
	return bounds.left >= worldBounds.left &&
		bounds.right < worldBounds.right &&
		bounds.bottom >= worldBounds.bottom &&
		bounds.top < worldBounds.top;
}

static void
removeFromList(CPtrList *list, ObjectInst *inst)
{
	CPtrNode *p, *next;
	for(p = list->first; p; p = next){
		next = p->next;
		if(p->item == inst)
			list->DeleteNode(p);
	}
}

void
RemoveInstFromSectors(ObjectInst *inst)
{
	int x, y;

	// Remove from out of bounds sector
	removeFromList(&outOfBoundsSector.buildings, inst);
	removeFromList(&outOfBoundsSector.buildings_overlap, inst);
	removeFromList(&outOfBoundsSector.bigbuildings, inst);
	removeFromList(&outOfBoundsSector.bigbuildings_overlap, inst);

	// Remove from all sectors
	for(y = 0; y < numSectorsY; y++)
		for(x = 0; x < numSectorsX; x++){
			Sector *s = GetSector(x, y);
			removeFromList(&s->buildings, inst);
			removeFromList(&s->buildings_overlap, inst);
			removeFromList(&s->bigbuildings, inst);
			removeFromList(&s->bigbuildings_overlap, inst);
		}
}

void
InsertInstIntoSectors(ObjectInst *inst)
{
	Sector *s;
	CPtrList *list;
	int x, xstart, xmid, xend;
	int y, ystart, ymid, yend;

	if(!IsInstInBounds(inst)){
		list = inst->m_isBigBuilding ? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
		return;
	}

	CRect bounds = inst->GetBoundRect();

	xstart = GetSectorIndexX(bounds.left);
	xend   = GetSectorIndexX(bounds.right);
	xmid   = GetSectorIndexX((bounds.left + bounds.right)/2.0f);
	ystart = GetSectorIndexY(bounds.bottom);
	yend   = GetSectorIndexY(bounds.top);
	ymid   = GetSectorIndexY((bounds.bottom + bounds.top)/2.0f);

	for(y = ystart; y <= yend; y++)
		for(x = xstart; x <= xend; x++){
			s = GetSector(x, y);
			if(x == xmid && y == ymid)
				list = inst->m_isBigBuilding ? &s->bigbuildings : &s->buildings;
			else
				list = inst->m_isBigBuilding ? &s->bigbuildings_overlap : &s->buildings_overlap;
			list->InsertItem(inst);
		}
}
