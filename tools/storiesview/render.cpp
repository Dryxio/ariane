#include "storiesview.h"

namespace Renderer
{

rw::ObjPipeline *buildingPipe;

rw::ObjPipeline *colourCodePipe;
rw::RGBA highlightColor;


struct InstAtm
{
	sGeomInstance *inst;
	rw::Atomic *atm;
};

int numOpaqueAtomics;
int numTransparentAtomics;
InstAtm opaqueRenderList[0x8000];
InstAtm transparentRenderList[0x8000];

void
reset(void)
{
	numOpaqueAtomics = 0;
	numTransparentAtomics = 0;
}

void
addToOpaqueRenderList(sGeomInstance *inst, rw::Atomic *a)
{
	opaqueRenderList[numOpaqueAtomics].inst = inst;
	opaqueRenderList[numOpaqueAtomics++].atm = a;
}

void
addToTransparentRenderList(sGeomInstance *inst, rw::Atomic *a)
{
	// TODO: sort
	transparentRenderList[numTransparentAtomics].inst = inst;
	transparentRenderList[numTransparentAtomics++].atm = a;
}

void
myRenderCB(rw::Atomic *atomic)
{
	if(gta::renderColourCoded)
		colourCodePipe->render(atomic);
	else if(highlightColor.red || highlightColor.green || highlightColor.blue){
		atomic->getPipeline()->render(atomic);
		gta::colourCode = highlightColor;
		gta::colourCode.alpha = 128;
		int32 zwrite, fog, aref;
		zwrite = GetRenderState(rw::ZWRITEENABLE);
		fog = rw::GetRenderState(rw::FOGENABLE);
		aref = rw::GetRenderState(rw::ALPHATESTREF);
		SetRenderState(rw::ZWRITEENABLE, 0);
		SetRenderState(rw::FOGENABLE, 0);
		SetRenderState(rw::ALPHATESTREF, 10);
		colourCodePipe->render(atomic);
		SetRenderState(rw::ZWRITEENABLE, zwrite);
		SetRenderState(rw::FOGENABLE, fog);
		SetRenderState(rw::ALPHATESTREF, aref);
	}else
		atomic->getPipeline()->render(atomic);
}

void
setColourCode(int n)
{
	gta::colourCode.red = n & 0xFF;
	gta::colourCode.green = n>>8 & 0xFF;
	gta::colourCode.blue = n>>16 & 0xFF;
	gta::colourCode.alpha = 255;
}

void
RenderInst(sGeomInstance *inst, rw::Atomic *a)
{
	static rw::RGBA black = { 0, 0, 0, 255 };
	static rw::RGBA red = { 255, 0, 0, 255 };
	static rw::RGBA green = { 0, 255, 0, 255 };
	static rw::RGBA blue = { 0, 0, 255, 255 };
	static rw::RGBA highlightCols[] = { black, green, red, blue };

	int id = inst->GetId();
	BuildingExt *be = GetBuildingExt(id);
	setColourCode(id);

	if(be->selected && be->highlight < HIGHLIGHT_SELECTION)
		be->highlight = HIGHLIGHT_SELECTION;
	highlightColor = highlightCols[be->highlight];

	a->render();
	highlightColor = black;
}

void
renderOpaque(void)
{
	int i;
	for(i = 0; i < numOpaqueAtomics; i++)
		RenderInst(opaqueRenderList[i].inst, opaqueRenderList[i].atm);
}

void
renderTransparent(void)
{
	int i;
	for(i = 0; i < numTransparentAtomics; i++)
		RenderInst(transparentRenderList[i].inst, transparentRenderList[i].atm);
}

void
renderEverythingColourCoded(void)
{
	rw::SetRenderState(rw::FOGENABLE, 0);
	SetRenderState(rw::ALPHATESTREF, 10);
	gta::renderColourCoded = 1;
	renderOpaque();
	renderTransparent();
	gta::renderColourCoded = 0;
}

void
drawEntityCube(CEntity *e)
{
	rw::Atomic *atomic;
	if(e->rslObject == 0){
		atomic = rw::Atomic::create();
		atomic->setGeometry(cubeGeo, 0);
		rw::Frame *f = rw::Frame::create();
		atomic->setFrame(f);
		f->matrix = *(rw::Matrix*)&e->placeable.matrix.matrix;
		f->matrix.optimize();
		e->rslObject = (int32)atomic;
	}else
		atomic = (rw::Atomic*)e->rslObject;
	atomic->render();
}

void
drawEntityCol(CEntity *e)
{
	rw::Matrix mat;
	CBaseModelInfo *mi = CModelInfo::Get(e->modelIndex);
	EntityExt *ee = (EntityExt*)e->vtable;

	if(drawUnmatched)
	//	if(ee->n == 1)
		if(ee->n > 0)
			return;
	if(!drawCol && ee->highlight == 0 && !ee->selected)
		return;

//	assert(e->area == 0);
	assert(mi);
	assert(mi->colModel);
	if(mi == nil || mi->colModel == nil)
		return;
	if(mi->type != MODELINFO_SIMPLE && mi->type != MODELINFO_TIME)
		return;
	if(drawUnnamed && mi->field0)
		return;
	CSimpleModelInfo *smi = (CSimpleModelInfo*)mi;
	CTimeModelInfo *tmi = nil;
	if(mi->type == MODELINFO_TIME)
		tmi = (CTimeModelInfo*)mi;

	bool lod = !!(smi->flags & 0x10);
	if(lod != drawLOD)
		return;
	if(tmi && !GetIsTimeInRange(tmi->timeOn, tmi->timeOff))
		return;

	rw::V3d pos = *(rw::V3d*)&e->placeable.matrix.matrix.pos;
	if(!ee->highlight && !ee->selected && pos.z < 100.0f && TheCamera.distanceTo(pos) > smi->drawDistances[0])
		return;

	ee->highlight = 0;

	mat = *(rw::Matrix*)&e->placeable.matrix.matrix;
	mat.optimize();
	if(drawWorld && !ee->selected)
		RenderColModelWire(mi->colModel, &mat, false);
	else{
		rw::RGBA col;
		if(gta::renderColourCoded)
			col = gta::colourCode;
		else{
			if(ee->selected)
				RenderColModelWire(mi->colModel, &mat, false);
			else
				RenderColModelWire(mi->colModel, &mat, true);

			srand((int)mi->colModel);
			int tmp = rand();
			col = *(rw::RGBA*)&tmp;
			col.alpha = 255;
		}
		RenderColMeshSolid(mi->colModel, &mat, col);
		if(drawBounds)
			RenderColBoxSolid(mi->colModel, &mat, col);
	}
}

void
renderColModels(void)
{
	CEntity *e;
	int i, n;

	cubeMat->color.red = 0;
	cubeMat->color.green = 255;
	cubeMat->color.blue = 255;
	pAmbient->setColor(1.0f, 1.0f, 1.0f);

	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		setColourCode(i | 0x10000);
		drawEntityCol(e);
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		setColourCode(i | 0x20000);
		drawEntityCol(e);
	}
	if(drawDummies){
		n = pDummyPool->GetSize();
		for(i = 0; i < n; i++){
			e = pDummyPool->GetSlot(i);
			if(e == nil)
				continue;
			setColourCode(i | 0x30000);
			drawEntityCol(e);
		}
	}
}

#ifdef LCS
#define PATHZSCALE (8.0f)
#else
#define PATHZSCALE (1.0f)
#endif

void
renderPathNodes(void)
{
	int i, j;
	static rw::Atomic *atomic;
	if(atomic == nil){
		atomic = rw::Atomic::create();
		atomic->setGeometry(cubeGeo, 0);
		rw::Frame *f = rw::Frame::create();
		atomic->setFrame(f);
	}

	static int numIndices;
	static int numVertices;
	static rw::RWDEVICE::Im3DVertex *vertices;
	static uint16 *indices;
	if(vertices == nil){
		numVertices = gpThePaths->m_numPathNodes;
		vertices = rwNewT(rw::RWDEVICE::Im3DVertex, numVertices, 0);
		numIndices = gpThePaths->m_numConnections*2;
		indices = rwNewT(uint16, numIndices, 0);
		int idx = 0;
		for(i = 0; i < gpThePaths->m_numPathNodes; i++){
			CPathNode *pn = &gpThePaths->m_pathNodes[i];

			vertices[i].setX(pn->x/8.0f);
			vertices[i].setY(pn->y/8.0f);
			vertices[i].setZ(pn->z/PATHZSCALE + 1.0f);
			if(i < gpThePaths->m_numCarPathNodes)
				vertices[i].setColor(255, 0, 0, 255);
			else
				vertices[i].setColor(0, 255, 0, 255);

			for(j = 0; j < pn->numLinks; j++){
				assert(i < numVertices);
				indices[idx++] = i;
				assert(gpThePaths->m_connections[pn->firstLink+j].idx < numVertices);
				indices[idx++] = gpThePaths->m_connections[pn->firstLink+j].idx;
				assert(idx <= numIndices);
			}
		}
		assert(idx == numIndices);
	}

	rw::im3d::Transform(vertices, numVertices, nil, rw::im3d::EVERYTHING);
	for(i = 0; i < numIndices; i += 10000)
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPELINELIST, indices+i, min(10000, numIndices-i));
	rw::im3d::End();

//	return;

	// Render connection flags
	rw::Frame *f = atomic->getFrame();
	for(i = 0; i < gpThePaths->m_numPathNodes; i++){
		CPathNode *pn = &gpThePaths->m_pathNodes[i];

		for(j = 0; j < pn->numLinks; j++){
			bool draw = false;
			cubeMat->color.red = 0;
			cubeMat->color.green = 0;
			cubeMat->color.blue = 0;
			if(gpThePaths->m_connections[pn->firstLink+j].bTrafficLight){
				cubeMat->color.red = 255;
				draw = true;
			}
			if(gpThePaths->m_connections[pn->firstLink+j].bCrossesRoad){
				cubeMat->color.green = 255;
				draw = true;
			}
			if(!draw)
				continue;
			int k = gpThePaths->m_connections[pn->firstLink+j].idx;

			rw::V3d pos;
			pos.x = (gpThePaths->m_pathNodes[i].x + gpThePaths->m_pathNodes[k].x)/8.0f/2;
			pos.y = (gpThePaths->m_pathNodes[i].y + gpThePaths->m_pathNodes[k].y)/8.0f/2;
			pos.z = (gpThePaths->m_pathNodes[i].z + gpThePaths->m_pathNodes[k].z)/PATHZSCALE/2 + 1.0f;

			f->translate(&pos, rw::COMBINEREPLACE);
			atomic->render();
		}
	}

/*
	// nodes as boxes
	rw::Frame *f = atomic->getFrame();
	for(i = 0; i < gpThePaths->m_numPathNodes; i++){
		CPathNode *pn = &gpThePaths->m_pathNodes[i];

		if(i >= 0 && i < gpThePaths->m_numCarPathNodes){
			cubeMat->color.red = 255;
			cubeMat->color.green = 0;
			cubeMat->color.blue = 0;
		}else if(i < gpThePaths->m_numPathNodes){
			cubeMat->color.red = 0;
			cubeMat->color.green = 255;
			cubeMat->color.blue = 0;
		}

		rw::V3d pos;
		pos.x = pn->x/8.0f;
		pos.y = pn->y/8.0f;
		pos.z = pn->z/PATHZSCALE;

		pos.z += 1.0f;

		f->translate(&pos, rw::COMBINEREPLACE);
		atomic->render();
	}
*/
}

}