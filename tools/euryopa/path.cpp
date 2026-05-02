#include "euryopa.h"
#include <vector>

void
PathNode::JumpTo(ObjectInst *inst)
{
	rw::V3d p = { x, y, z };
	if(inst) rw::V3d::transformPoints(&p, &p, 1, &inst->m_matrix);
	TheCamera.setTarget(p);
}

bool
PathNode::isDetached(void)
{
	return tabId == 1 || tabId == 3;
}

namespace Path {

typedef std::vector<PathNode> NodeTable;
NodeTable pedNodes;
NodeTable carNodes;
NodeTable pedDetachedNodes;
NodeTable carDetachedNodes;

NodeTable *nodeTables[] = { &carNodes, &carDetachedNodes, &pedNodes, &pedDetachedNodes };

void
FillLanes(PathNode *seg)
{
	int done = 0;
	PathNode *prev = nil;
	for(int i = 0; i < 12; i++){
		PathNode *nd = &seg[i];
		if(nd->type != PathNode::NodeExternal) continue;
		while(nd->numLinks <= 2){
			PathNode *ln = &seg[nd->links[0]];
			if(nd->numLinks == 2 && ln == prev) ln = &seg[nd->links[1]];
			if(ln->lanesInX < 0){
				ln->lanesInX = nd->lanesInX;
				ln->lanesOutX = nd->lanesOutX;
			}else break;
			prev = nd;
			nd = ln;
		}
	}
}

void
AddNode(PathType type, int id, PathNode node)
{
	NodeTable *tab;
	node.numLinks = 0;
	node.objId = id;
	int tabid;
	if(id < 0){
		if(type == PedPath)
			tabid = 3;
		else
			tabid = 1;
	}else{
		ObjectDef *obj = GetObjectDef(id);
		if(obj == nil)
			return;
		if(type == PedPath){
			obj->m_pedPathIndex = pedNodes.size() / 12;
			tabid = 2;
		}else{
			obj->m_carPathIndex = carNodes.size() / 12;
			tabid = 0;
		}
	}
	node.tabId = tabid;
	node.water = type == WaterPath;
	tab = nodeTables[tabid];
	NodeTable &t = *tab;
	node.idx = t.size()/12;
	node.lanesInX = node.lanesOutX = -1;
	if(node.type == PathNode::NodeExternal){
		node.lanesInX = node.lanesIn;
		node.lanesOutX = node.lanesOut;
	}
	t.push_back(node);
	if(t.size() % 12 == 0){
		int idx = t.size() / 12 - 1;
		for(int i = 0; i < 12; i++){
			PathNode *nd = &t[idx*12+i];
			if(nd->link >= 0){
				nd->links[nd->numLinks++] = nd->link;
				PathNode *ln = &t[idx*12+nd->link];
				ln->links[ln->numLinks++] = i;
			}
		}
		if(type != PedPath)
			FillLanes(&t[idx*12]);
	}
}

PathNode*
GetPedNode(int base, int i)
{
	PathNode *n = &pedNodes[base*12+i];
	return n->type == PathNode::NodeNone ? nil : n;
}

PathNode*
GetCarNode(int base, int i)
{
	PathNode *n = &carNodes[base*12+i];
	return n->type == PathNode::NodeNone ? nil : n;
}

PathNode*
GetDetachedPedNode(int base, int i)
{
	if(base*12+i >= pedDetachedNodes.size())
		return nil;
	PathNode *n = &pedDetachedNodes[base*12+i];
	return n->type == PathNode::NodeNone ? nil : n;
}

PathNode*
GetDetachedCarNode(int base, int i)
{
	if(base*12+i >= carDetachedNodes.size())
		return nil;
	PathNode *n = &carDetachedNodes[base*12+i];
	return n->type == PathNode::NodeNone ? nil : n;
}

static const rw::RGBA red = { 255, 0, 0, 255 };
static const rw::RGBA green = { 0, 255, 0, 255 };
static const rw::RGBA blue = { 0, 0, 255, 255 };
static const rw::RGBA cyan = { 0, 255, 255, 255 };
static const rw::RGBA magenta = { 255, 0, 255, 255 };
static const rw::RGBA yellow = { 255, 255, 0, 255 };
static const rw::RGBA white = { 255, 255, 255, 255 };

float gPathDrawDist = 300.0f;

PathNode *hoveredNode;
PathNode *guiHoveredNode;
PathNode *selectedNode;

static void
DrawNodePatch(rw::V3d positions[], NodeTable &nodes, int idx, ObjectInst *inst, rw::RGBA col)
{
	int n;
	for(n = 0; n < 12; n++){
		PathNode *nd = &nodes[idx+n];
		if(nd->type == PathNode::NodeNone) break;
		rw::V3d p = {nd->x,nd->y,nd->z + 1.0f};
		positions[n] = p;
	}

	if(inst) rw::V3d::transformPoints(positions, positions, n, &inst->m_matrix);

	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);
	for(int i = 0; i < n; i++){
		PathNode *nd = &nodes[idx+i];
		if(TheCamera.distanceTo(positions[i]) > gPathDrawDist)
			continue;
		rw::RGBA c = nd == selectedNode ? white : col;
		CSphere sphere;
		sphere.radius = 1.0f;
		sphere.center = positions[i];
		if(SphereIntersect(sphere, ray) || nd == guiHoveredNode){
			hoveredNode = nd;
			c = cyan;
		}
		if(nodes[idx+i].type == PathNode::NodeInternal)
			RenderWireSphere(&sphere, c, nil);
		else
			RenderSphereAsWireBox(&sphere, c, nil);
	}

	for(int i = 0; i < n; i++){
		PathNode *nd = &nodes[idx+i];

		if(nd->link < 0) continue;
		rw::V3d p1 = positions[i];
		rw::V3d p2 = positions[nd->link];
		if(TheCamera.distanceTo(p1) > gPathDrawDist)
			continue;

		int selected = nd == selectedNode || &nodes[idx+nd->link] == selectedNode;
		rw::RGBA c = nd == hoveredNode ? cyan :
			selected ? white :
			nd->linkType ? yellow : col;
		RenderLine(p1, p2, c, c);
	}
}

static void
DrawLanes(rw::V3d positions[], NodeTable &nodes, int idx, rw::RGBA col)
{
	for(int i = 0; i < 12; i++){
		PathNode *nd = &nodes[idx+i];
		if(nd->type == PathNode::NodeNone) break;
		if(nd->link < 0) continue;
		rw::RGBA c = nd == selectedNode ? white : nd->linkType ? yellow : col;
		rw::V3d p1 = positions[i];
		rw::V3d p2 = positions[nd->link];
		// make sure p1 is external is possible
		if(nd->type == PathNode::NodeInternal){
			nd = &nodes[idx+nd->link];
			p1 = p2;
			p2 = positions[i];
		}
		if(TheCamera.distanceTo(p1) > gPathDrawDist)
			continue;
		rw::V3d dir = sub(p2, p1);
		rw::V3d up = { 0.0f, 0.0f, 1.0f };
		rw::V3d right = normalize(cross(up, dir));
		rw::V3d laneoff = scale(right,2.5);

		float laneOff = isIII() ? nd->laneOffsetIII() : nd->laneOffset();
		rw::V3d r1 = add(p1,scale(right,laneOff*LaneWidth));
		rw::V3d r2 = add(p2,scale(right,laneOff*LaneWidth));
		rw::V3d l1 = sub(p1,scale(right,laneOff*LaneWidth));
		rw::V3d l2 = sub(p2,scale(right,laneOff*LaneWidth));

		for(int l = 0; l < nd->lanesOutX; l++)
			RenderLine(sub(l1,scale(right,LaneWidth*l)), sub(l2,scale(right,LaneWidth*l)), green, green);
		for(int l = 0; l < nd->lanesInX; l++)
			RenderLine(add(r1,scale(right,LaneWidth*l)), add(r2,scale(right,LaneWidth*l)), green, green);
	}
}

void
RenderPedPaths(void)
{
	rw::V3d positions[12];
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil)
			continue;
		if(obj->m_pedPathIndex >= 0)
			DrawNodePatch(positions, pedNodes, obj->m_pedPathIndex*12, inst, magenta);
	}
	for(uint i = 0; i < pedDetachedNodes.size(); i += 12)
		DrawNodePatch(positions, pedDetachedNodes, i, nil, magenta);
}

void
RenderCarPaths(void)
{
	rw::V3d positions[12];
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil)
			continue;
		if(obj->m_carPathIndex >= 0){
			DrawNodePatch(positions, carNodes, obj->m_carPathIndex*12, inst, red);
			DrawLanes(positions, carNodes, obj->m_carPathIndex*12, green);
		}
	}
	for(uint i = 0; i < carDetachedNodes.size(); i += 12){
		DrawNodePatch(positions, carDetachedNodes, i, nil, red);
		DrawLanes(positions, carDetachedNodes, i, green);
	}
}

}
