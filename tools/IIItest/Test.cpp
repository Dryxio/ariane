#include "III.h"

#include "Test.h"
#include "DebugDraw.h"

// to test various things, collisions right now

//#define COLTEST
//#define FOOTEST

static CColSphere sphere1;
static CColSphere sphere2;
static CColBox box1;
static CColLine line1;
static CColTriangle tri1;
static CVector verts[3];

static rw::RGBA white = { 255, 255, 255, 255 };
static rw::RGBA red = { 255, 0, 0, 255 };
static rw::RGBA green = { 0, 255, 0, 255 };
static rw::RGBA blue = { 0, 0, 255, 255 };
static CMatrix ident;
static CMatrix modelmat;

class CTestObject : public CPhysical
{
public:
	void *operator new(size_t s) { return (void*)rwNewT(uint8, s, 0); };
	void operator delete(void *p, size_t) { rwFree(p); }
	CTestObject(int id);
	void ProcessControl(void);
};

static CTestObject *car;

CTestObject::CTestObject(int id)
{
	m_type = ENTITY_TYPE_VEHICLE;
	m_status = STATUS_SIMPLE;

	SetModelIndex(id);
	CVehicleModelInfo *mi = (CVehicleModelInfo*)CModelInfo::GetModelInfo(GetModelIndex());
	assert(mi->m_type == VEHICLEMODELINFO);
	tHandlingData *handling = cHandlingDataMgr::GetHandlingData(mi->m_handlingId);
	m_fMass = handling->fMass;
	m_fTurnMass = handling->fTurnMass;
	m_vecCentreOfMass = handling->CenterOfMass;
	m_fAirResistance = handling->Dimensions.x * handling->Dimensions.z / m_fMass;
}

void
CTestObject::ProcessControl(void)
{
	ApplyMoveSpeed();
	ApplyTurnSpeed();
//	ApplyGravity();
	ApplyFriction();
	ApplyAirResistance();
	m_matrix.Reorthogonalise();
}

void
CTest::Init(void)
{
#ifdef FOOTEST
	int id = 105;	// cheetah
	CStreaming::RequestModel(id, 1);
	CStreaming::LoadAllRequestedModels();

	car = new CTestObject(id);
	rw::Matrix mat;
	rw::V3d pos = { 0.0f, 0.0f, 500.0f };
	mat.translate(&pos, rw::COMBINEREPLACE);
	car->SetTransform(&mat);

	car->GetMatrix().UpdateRW();
	car->UpdateRwFrame();

//	car->m_vecMoveSpeed = CVector(0.0f, 0.01f, 0.0f);
//	car->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
//	car->ApplyTurnForce(CVector(0.0f, 0.0f, 500.0f),
//		CVector(0.0f, 1.0f, -0.3f));

	// Set camera up into the sky
	TheCamera.m_target.set(0.0f, 0.0f, 500.0f);
	TheCamera.m_position.set(10.0f, 0.0f, 505.0f);
#endif

#ifdef COLTEST
	// Set camera up into the sky
	TheCamera.m_target.set(0.0f, 0.0f, 500.0f);
	TheCamera.m_position.set(10.0f, 0.0f, 505.0f);

	ident.m_matrix.setIdentity();
	modelmat.m_matrix.setIdentity();

	sphere1.Set(3.0f, CVector(0.0f, 0.0f, 500.0f), 0, 0);
	sphere2.Set(3.0f, CVector(0.0f, 10.0f, 500.0f), 0, 0);
	box1.Set(CVector(5.0, 5.0, 500.0f), CVector(10.0, 10.0, 510.0f), 0, 0);

	verts[0] = CVector(0.0f, 0.0f, 500.0f);
	verts[1] = CVector(0.0f, 5.0f, 500.0f);
	verts[2] = CVector(3.0f, 0.0f, 503.0f);
	tri1.Set(0, 1, 2, 0);

	CStreaming::RequestModel(105, 1);
	CStreaming::LoadAllRequestedModels();
	assert(CModelInfo::GetModelInfo(105)->GetColModel());
	modelmat.m_matrix.pos.set(0.0f, 0.0f, 500.0f);
	modelmat.m_matrix.optimize();
#endif
}

void
CTest::Update(void)
{
#ifdef FOOTEST
//	car->ApplyMoveForce(0.0f, 0.1f, 0.0f);

	car->ProcessControl();
	car->GetMatrix().UpdateRW();
	car->UpdateRwFrame();
#endif
#ifdef COLTEST
//	sphere1.center = TheCamera.m_target;
//	line1.Set(TheCamera.m_target, CVector(TheCamera.m_target) + CVector(2.0f, 2.0f, 5.0f));
//	line1.Set(TheCamera.m_target, CVector(TheCamera.m_target) + CVector(0.0f, 0.0f, 5.0f));
	line1.Set(CVector(TheCamera.m_target) + CVector(0.0f, 0.0f, 5.0f), TheCamera.m_target);
//	modelmat.m_matrix.pos = TheCamera.m_target;
#endif
}

void
CTest::Render(void)
{
#ifdef FOOTEST
	ActivateDirectional();
	SetAmbientColoursForPedsCarsAndObjects();
	car->Render();
#endif
#ifdef COLTEST
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::engine->imtexture = nil;


//	CDebugDraw::RenderWireSphere(sphere2.center, sphere2.radius, white);
//	CDebugDraw::RenderWireBox(ident, box1.min, box1.max, white);

//	CDebugDraw::RenderWireTri(verts, tri1.a, tri1.b, tri1.c, white);
//	CColTrianglePlane plane;
//	plane.Set(verts, tri1);

//	if(CCollision::TestLineSphere(line1, sphere2))
//	if(CCollision::TestVerticalLineBox(line1, box1))
//	if(CCollision::TestLineBox(line1, box1))
//	if(CCollision::TestLineTriangle(line1, verts, tri1, plane))
//		CDebugDraw::RenderLine(line1.p0, line1.p1, red, red);
//	else
//		CDebugDraw::RenderLine(line1.p0, line1.p1, white, white);


//	if(CCollision::TestSphereSphere(sphere1, sphere2))
//	if(CCollision::TestSphereBox(sphere1, box1))
//	if(CCollision::TestSphereTriangle(sphere1, verts, tri1, plane))
//		CDebugDraw::RenderWireSphere(sphere1.center, sphere1.radius, red);
//	else
//		CDebugDraw::RenderWireSphere(sphere1.center, sphere1.radius, white);

	CColPoint point;
	float t = 100.0f;
//	if(CCollision::ProcessLineSphere(line1, sphere2, point, t)){
//	if(CCollision::ProcessLineTriangle(line1, verts, tri1, plane, point, t)){
//	if(CCollision::ProcessVerticalLineTriangle(line1, verts, tri1, plane, point, t, nil)){
//	if(CCollision::ProcessLineBox(line1, box1, point, t)){
//		CDebugDraw::RenderLine(line1.p0, line1.p1, red, red);
//		CDebugDraw::RenderWireSphere(point.point, 0.05f, blue);
//	}else
//		CDebugDraw::RenderLine(line1.p0, line1.p1, white, white);

//	float r = sphere1.radius*sphere1.radius;
//	if(CCollision::ProcessSphereSphere(sphere1, sphere2, point, r)){
//	if(CCollision::ProcessSphereBox(sphere1, box1, point, r)){
//	if(CCollision::ProcessSphereTriangle(sphere1, verts, tri1, plane, point, r)){
//		CDebugDraw::RenderWireSphere(sphere1.center, sphere1.radius, red);
//		CDebugDraw::RenderWireSphere(sphere1.center, sqrt(r), green);
//		CDebugDraw::RenderWireSphere(point.point, 0.05f, blue);
//	}else
//		CDebugDraw::RenderWireSphere(sphere1.center, sphere1.radius, white);

	CCollision::DrawColModel(modelmat, *CModelInfo::GetModelInfo(105)->GetColModel());

//	if(CCollision::TestLineOfSight(line1, modelmat, *CModelInfo::GetModelInfo(105)->GetColModel(), false))
//		CDebugDraw::RenderLine(line1.p0, line1.p1, red, red);
//	else
//		CDebugDraw::RenderLine(line1.p0, line1.p1, white, white);

//	if(CCollision::ProcessLineOfSight(line1, modelmat, *CModelInfo::GetModelInfo(105)->GetColModel(), point, t, false)){
	if(CCollision::ProcessVerticalLine(line1, modelmat, *CModelInfo::GetModelInfo(105)->GetColModel(), point, t, false, NULL)){
		CDebugDraw::RenderLine(line1.p0, line1.p1, red, red);
		CDebugDraw::RenderWireSphere(point.point, 0.05f, blue);
	}else
		CDebugDraw::RenderLine(line1.p0, line1.p1, white, blue);

	CDebugDraw::RenderAndEmptyRenderBuffer();
#endif
}
