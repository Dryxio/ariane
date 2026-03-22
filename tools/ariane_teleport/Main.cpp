#include <plugin.h>
#include <CPlayerPed.h>
#include <CWorld.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

#if defined(GTASA)
#include <CGame.h>
#include <CTimeCycle.h>
#include <CColStore.h>
#include <CFileLoader.h>
#include <CFileObjectInstance.h>
#include <CStreaming.h>
#include <CIplStore.h>
#include <CEntity.h>
#include <CObject.h>
#include <CTheScripts.h>
#include <CMatrix.h>

enum {
	ARIANE_STREAMING_MISSION_REQUIRED = 0x4,
	ARIANE_STREAMING_KEEP_IN_MEMORY = 0x8,
	ARIANE_STREAMING_PRIORITY_REQUEST = 0x10
};
#endif

using namespace plugin;

#if defined(GTASA)
static void
LogHotReload(const char *fmt, ...)
{
	FILE *f = fopen("ariane_plugin_log.txt", "a");
	if(!f)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fputc('\n', f);
	fclose(f);
}

struct DebugTrackedEntity {
	CEntity *entity;
	int framesLeft;
	int modelId;
	CVector pos;
};

static DebugTrackedEntity gTrackedAdds[16];

static CEntity* FindEntityByModelNearPos(int modelId, float x, float y, float z, float radius);

static void
TrackAddedEntity(CEntity *entity, int modelId, const CVector& pos)
{
	if(!entity)
		return;
	for(int i = 0; i < 16; i++){
		if(gTrackedAdds[i].framesLeft <= 0){
			gTrackedAdds[i].entity = entity;
			gTrackedAdds[i].framesLeft = 120;
			gTrackedAdds[i].modelId = modelId;
			gTrackedAdds[i].pos = pos;
			return;
		}
	}
}

static void
LogTrackedAdds(void)
{
	for(int i = 0; i < 16; i++){
		if(gTrackedAdds[i].framesLeft <= 0 || gTrackedAdds[i].entity == NULL)
			continue;
		CEntity *e = gTrackedAdds[i].entity;
		bool entityValid = IsEntityPointerValid(e);
		bool objectInPool = false;
		bool objectInWorld = false;
		void *collisionList = NULL;
		int doNotRender = -1;
		if(e->m_nType == ENTITY_TYPE_OBJECT){
			CObject *obj = static_cast<CObject*>(e);
			objectInPool = IsObjectPointerValid_NotInWorld(obj);
			objectInWorld = IsObjectPointerValid(obj);
			collisionList = obj->m_pCollisionList;
			doNotRender = obj->m_nObjectFlags.bDoNotRender;
		}
		CEntity *found = FindEntityByModelNearPos(
			gTrackedAdds[i].modelId,
			gTrackedAdds[i].pos.x,
			gTrackedAdds[i].pos.y,
			gTrackedAdds[i].pos.z,
			50.0f
		);
		if(gTrackedAdds[i].framesLeft == 120 || gTrackedAdds[i].framesLeft == 90 ||
		   gTrackedAdds[i].framesLeft == 60 || gTrackedAdds[i].framesLeft == 30 ||
		   gTrackedAdds[i].framesLeft == 1)
		{
			CBaseModelInfo *mi = CModelInfo::GetModelInfo(gTrackedAdds[i].modelId);
			void *baseRw = mi ? mi->m_pRwObject : NULL;
			int alpha = mi ? mi->m_nAlpha : -1;
			if(entityValid){
				CVector pos = e->GetPosition();
				LogHotReload("A track model=%d entity=%p framesLeft=%d entityValid=1 objectInPool=%d objectInWorld=%d rw=%p baseRw=%p alpha=%d visible=%d onScreen=%d area=%d doNotRender=%d collisionList=%p found=%p pos=(%.3f,%.3f,%.3f)",
					gTrackedAdds[i].modelId, e, gTrackedAdds[i].framesLeft,
					objectInPool, objectInWorld, e->m_pRwObject, baseRw, alpha,
					e->bIsVisible, e->GetIsOnScreen(), e->m_nAreaCode, doNotRender, collisionList, found,
					pos.x, pos.y, pos.z);
			}else{
				LogHotReload("A track model=%d entity=%p framesLeft=%d entityValid=0 objectInPool=%d objectInWorld=%d baseRw=%p alpha=%d doNotRender=%d collisionList=%p found=%p",
					gTrackedAdds[i].modelId, e, gTrackedAdds[i].framesLeft,
					objectInPool, objectInWorld, baseRw, alpha, doNotRender, collisionList, found);
			}
		}
		if(!entityValid && !objectInPool && found == NULL){
			gTrackedAdds[i].framesLeft = 0;
			gTrackedAdds[i].entity = NULL;
		}
		gTrackedAdds[i].framesLeft--;
		if(gTrackedAdds[i].framesLeft <= 0)
			gTrackedAdds[i].entity = NULL;
	}
}

// Find a building/dummy entity by model ID near a position.
// Returns the closest match within radius, or NULL.
static CEntity*
FindEntityByModelNearPos(int modelId, float x, float y, float z, float radius)
{
	CEntity *found = NULL;
	float bestDist = radius * radius;

	const auto ConsiderMatches = [&](CEntity **entities, short count) {
		for(int i = 0; i < count; i++){
			if(entities[i]->m_nModelIndex != modelId)
				continue;
			CVector &epos = entities[i]->GetPosition();
			float dx = epos.x - x;
			float dy = epos.y - y;
			float dz = epos.z - z;
			float dist = dx*dx + dy*dy + dz*dz;
			if(dist < bestDist){
				bestDist = dist;
				found = entities[i];
			}
		}
	};

	short count = 0;
	CEntity *entities[512];
	CVector pos(x, y, z);

	CWorld::FindObjectsInRange(pos, radius, true, &count, 512, entities,
		true,   // buildings
		false,  // vehicles
		false,  // peds
		true,   // objects
		true);  // dummies
	ConsiderMatches(entities, count);

	short lodCount = 0;
	CEntity *lodEntities[512];
	CWorld::FindLodOfTypeInRange(modelId, pos, radius, true, &lodCount, 512, lodEntities);
	ConsiderMatches(lodEntities, lodCount);
	return found;
}

static void
PromoteToBigBuildingIfNeeded(CEntity *entity)
{
	if(!entity)
		return;
	const CBaseModelInfo *mi = CModelInfo::GetModelInfo(entity->m_nModelIndex);
	if(!mi)
		return;
	if(entity->m_nNumLodChildren == 0 && mi->m_fDrawDistance <= 300.0f)
		return;
	if(entity->bIsBIGBuilding)
		return;
	CWorld::Remove(entity);
	entity->SetupBigBuilding();
	CWorld::Add(entity);
}

static void
SetupBigBuildingIfNeededBeforeAdd(CEntity *entity)
{
	if(!entity)
		return;
	const CBaseModelInfo *mi = CModelInfo::GetModelInfo(entity->m_nModelIndex);
	if(!mi)
		return;
	if(entity->m_nNumLodChildren == 0 && mi->m_fDrawDistance <= 300.0f)
		return;
	if(entity->bIsBIGBuilding)
		return;
	entity->SetupBigBuilding();
}

// Process entity-level commands from ariane_reload_entities.txt
static void
ProcessEntityReload(void)
{
	FILE *f = fopen("ariane_reload_entities.txt", "r");
	if(!f) return;

	struct PendingLodLink {
		CEntity *entity;
		int lodModelId;
		float lodX, lodY, lodZ;
	};
	PendingLodLink pendingLodLinks[256];
	int numPendingLodLinks = 0;

	char line[256];
	while(fgets(line, sizeof(line), f)){
		if(line[0] == 'A'){
			// A modelId x y z qx qy qz qw area lodModelId lodX lodY lodZ
			int modelId, area, lodModelId;
			float x, y, z, qx, qy, qz, qw, lodX, lodY, lodZ;
			if(sscanf(line + 2, "%d %f %f %f %f %f %f %f %d %d %f %f %f",
				&modelId, &x, &y, &z, &qx, &qy, &qz, &qw,
				&area, &lodModelId, &lodX, &lodY, &lodZ) == 13)
			{
				CVector pos(x, y, z);
				LogHotReload("A begin model=%d pos=(%.3f,%.3f,%.3f) area=%d lod=%d",
					modelId, x, y, z, area & 0xFF, lodModelId);
				CStreaming::RequestModel(modelId,
					ARIANE_STREAMING_MISSION_REQUIRED |
					ARIANE_STREAMING_KEEP_IN_MEMORY |
					ARIANE_STREAMING_PRIORITY_REQUEST);
				CColStore::RequestCollision(pos, area & 0xFF);
				CStreaming::LoadAllRequestedModels(false);
				CColStore::EnsureCollisionIsInMemory(pos);
				CBaseModelInfo *mi = CModelInfo::GetModelInfo(modelId);
				if(mi)
					mi->m_nAlpha = 255;
				CColModel *cm = CModelInfo::GetColModel(modelId);
				LogHotReload("A modelinfo model=%d mi=%p baseRw=%p col=%p alpha=%d",
					modelId, mi, mi ? mi->m_pRwObject : NULL, cm, mi ? mi->m_nAlpha : -1);

				CObject *obj = CObject::Create(modelId);
				if(obj){
					CVector previewPos = pos;
					obj->m_nObjectType = OBJECT_MISSION;
					obj->bDontStream = false;
					obj->bStreamingDontDelete = true;
					obj->SetPosn(previewPos);
					obj->SetIsStatic(true);
					obj->m_nAreaCode = (unsigned char)(area & 0xFF);
					obj->bUnderwater |= (area & 0x400) != 0;
					obj->bTunnel |= (area & 0x800) != 0;
					obj->bTunnelTransition |= (area & 0x1000) != 0;
					obj->bUnimportantStream |= (area & 0x100) != 0;

					const float cqx = -qx;
					const float cqy = -qy;
					const float cqz = -qz;
					const float cqw = qw;

					CMatrix mat;
					float x2 = cqx+cqx, y2 = cqy+cqy, z2 = cqz+cqz;
					float xx = cqx*x2, xy = cqx*y2, xz = cqx*z2;
					float yy = cqy*y2, yz = cqy*z2, zz = cqz*z2;
					float wx = cqw*x2, wy = cqw*y2, wz = cqw*z2;

					mat.right.x = 1.0f-(yy+zz); mat.right.y = xy+wz;         mat.right.z = xz-wy;
					mat.up.x    = xy-wz;         mat.up.y    = 1.0f-(xx+zz); mat.up.z    = yz+wx;
					mat.at.x    = xz+wy;         mat.at.y    = yz-wx;         mat.at.z    = 1.0f-(xx+yy);
					mat.pos.x   = previewPos.x;  mat.pos.y   = previewPos.y;  mat.pos.z   = previewPos.z;

					obj->SetMatrix(mat);
					obj->CreateRwObject();
					obj->UpdateRwMatrix();
					obj->UpdateRwFrame();
					CPlayerPed *player = FindPlayerPed();
					CVector playerPos = player ? player->GetPosition() : CVector(0.0f, 0.0f, 0.0f);
					float pdx = playerPos.x - previewPos.x;
					float pdy = playerPos.y - previewPos.y;
					float pdz = playerPos.z - previewPos.z;
					float playerDist = sqrtf(pdx*pdx + pdy*pdy + pdz*pdz);
					LogHotReload("A created obj=%p rw=%p area=%d type=%d static=%d visible=%d onScreen=%d previewPos=(%.3f,%.3f,%.3f) playerPos=(%.3f,%.3f,%.3f) dist=%.3f",
						obj, obj->m_pRwObject, obj->m_nAreaCode, obj->m_nObjectType, obj->bIsStatic,
						obj->bIsVisible, obj->GetIsOnScreen(),
						previewPos.x, previewPos.y, previewPos.z,
						playerPos.x, playerPos.y, playerPos.z, playerDist);

					CEntity *e = obj;
					if(lodModelId >= 0 && numPendingLodLinks < 256){
						pendingLodLinks[numPendingLodLinks].entity = e;
						pendingLodLinks[numPendingLodLinks].lodModelId = lodModelId;
						pendingLodLinks[numPendingLodLinks].lodX = lodX;
						pendingLodLinks[numPendingLodLinks].lodY = lodY;
						pendingLodLinks[numPendingLodLinks].lodZ = lodZ;
						numPendingLodLinks++;
					}else{
						SetupBigBuildingIfNeededBeforeAdd(e);
						CWorld::Add(e);
						TrackAddedEntity(e, modelId, previewPos);
						CEntity *found = FindEntityByModelNearPos(modelId, x, y, z, 10.0f);
						LogHotReload("A added obj=%p found=%p pos=(%.3f,%.3f,%.3f)",
							obj, found, obj->GetPosition().x, obj->GetPosition().y, obj->GetPosition().z);
					}
				}else{
					LogHotReload("A create failed model=%d", modelId);
				}
			}
		}else if(line[0] == 'M'){
			// M modelId oldX oldY oldZ newX newY newZ qx qy qz qw
			int modelId;
			float ox, oy, oz, nx, ny, nz, qx, qy, qz, qw;
			if(sscanf(line + 2, "%d %f %f %f %f %f %f %f %f %f %f",
				&modelId, &ox, &oy, &oz, &nx, &ny, &nz,
				&qx, &qy, &qz, &qw) == 11)
			{
				CEntity *e = FindEntityByModelNearPos(modelId, ox, oy, oz, 50.0f);
				if(e){
					CWorld::Remove(e);
					e->SetPosn(nx, ny, nz);

					// Ariane stores instance rotation in the same convention as the IPL.
					// Both the editor preview and SA's LoadObjectInstance build the
					// world matrix from the conjugated quaternion.
					const float cqx = -qx;
					const float cqy = -qy;
					const float cqz = -qz;
					const float cqw = qw;

					// Build rotation matrix from the conjugated quaternion
					CMatrix mat;
					float x2 = cqx+cqx, y2 = cqy+cqy, z2 = cqz+cqz;
					float xx = cqx*x2, xy = cqx*y2, xz = cqx*z2;
					float yy = cqy*y2, yz = cqy*z2, zz = cqz*z2;
					float wx = cqw*x2, wy = cqw*y2, wz = cqw*z2;

					mat.right.x = 1.0f-(yy+zz); mat.right.y = xy+wz;         mat.right.z = xz-wy;
					mat.up.x    = xy-wz;         mat.up.y    = 1.0f-(xx+zz); mat.up.z    = yz+wx;
					mat.at.x    = xz+wy;         mat.at.y    = yz-wx;         mat.at.z    = 1.0f-(xx+yy);
					mat.pos.x   = nx;            mat.pos.y   = ny;            mat.pos.z   = nz;

					e->SetMatrix(mat);
					CWorld::Add(e);
					PromoteToBigBuildingIfNeeded(e);
					e->UpdateRwFrame();
				}
			}
		}else if(line[0] == 'D'){
			// D modelId oldX oldY oldZ
			int modelId;
			float ox, oy, oz;
			if(sscanf(line + 2, "%d %f %f %f", &modelId, &ox, &oy, &oz) == 4){
				CEntity *e = FindEntityByModelNearPos(modelId, ox, oy, oz, 50.0f);
				if(e){
					CWorld::Remove(e);
					delete e;
				}
			}
		}
	}

	for(int i = 0; i < numPendingLodLinks; i++){
		PendingLodLink &link = pendingLodLinks[i];
		CEntity *lod = FindEntityByModelNearPos(link.lodModelId, link.lodX, link.lodY, link.lodZ, 50.0f);
		if(!lod || !link.entity)
			continue;
		link.entity->m_pLod = lod;
		lod->m_nNumLodChildren++;
		PromoteToBigBuildingIfNeeded(lod);
		SetupBigBuildingIfNeededBeforeAdd(link.entity);
		CWorld::Add(link.entity);
	}

	fclose(f);
	remove("ariane_reload_entities.txt");
}
#endif

class ArianeTeleport {
public:
	ArianeTeleport() {
		Events::gameProcessEvent += [] {
			CPlayerPed *player = FindPlayerPed();
			if(!player) return;
			LogTrackedAdds();

			// --- Phase 1: one-time teleport from editor ---
			static bool teleportDone = false;
			if(!teleportDone){
				FILE *f = fopen("ariane_teleport.txt", "r");
				if(!f){ teleportDone = true; }
				else {
					float x, y, z, heading;
					int area = 0;
					int n = fscanf(f, "%f %f %f %f %d", &x, &y, &z, &heading, &area);
					fclose(f);
					remove("ariane_teleport.txt");

					if(n >= 4){
#if defined(GTASA)
						CGame::currArea = area;
						player->m_nAreaCode = area;
						player->Teleport(CVector(x, y, z), false);
						CVector loadPos(x, y, z);
						CStreaming::LoadScene(&loadPos);
						CTimeCycle::StopExtraColour(false);
#else
						player->Teleport(CVector(x, y, z));
#endif
						player->SetHeading(heading);
					}
					teleportDone = true;
				}
			}

#if defined(GTASA)
			// --- Phase 2: hot reload streaming IPLs ---
			{
				FILE *f = fopen("ariane_reload.txt", "r");
				if(f){
					char line[64];
					int slots[256];
					int numSlots = 0;

					while(fgets(line, sizeof(line), f) && numSlots < 256){
						char *end = line + strlen(line) - 1;
						while(end >= line && (*end == '\n' || *end == '\r' || *end == ' '))
							*end-- = '\0';
						if(line[0] == '\0') continue;

						int slot = CIplStore::FindIplSlot(line);
						if(slot >= 0)
							slots[numSlots++] = slot;
					}
					fclose(f);
					remove("ariane_reload.txt");

					if(numSlots > 0){
						CVector reloadPos = player->GetPosition();
						for(int i = 0; i < numSlots; i++)
							CIplStore::RemoveIplAndIgnore(slots[i]);
						for(int i = 0; i < numSlots; i++)
							CIplStore::RequestIplAndIgnore(slots[i]);
						CStreaming::LoadAllRequestedModels(false);
						CColStore::EnsureCollisionIsInMemory(reloadPos);
					}
				}
			}

			// --- Phase 3: hot reload text IPL entities ---
			ProcessEntityReload();
#endif
		};
	}
} arianeTeleportInstance;
