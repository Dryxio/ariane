#include "III.h"
#include "General.h"
#include "Camera.h"
#include "Draw.h"

CCamera TheCamera;

void
CCamera::Init(void)
{
	memset(this, 0, sizeof(CCamera));

	Cams[0].Init();
	Cams[1].Init();
	Cams[2].Init();
	ActiveCam = 0;

	LODDistMultiplier = 1.0f;

	Cams[2].Source = CVector(1286.0f, -211.0f, 50.0f);
	CVector target(1155.0f, -190.0f, -18.0f);
	Cams[2].Front = target - Cams[2].Source;
	Cams[2].m_fCameraDistance = Cams[2].Front.Magnitude();
	Cams[2].Front.Normalise();
	Cams[2].Up = CVector(0.0f, 0.0f, 1.0f);
	ActiveCam = 2;
	InitialiseCameraForDebugMode();
}

bool
CCamera::IsSphereVisible(const CVector &center, float radius)
{
	CVector c = m_cameraMatrix * center;
	if(c.y + radius < CDraw::GetNearClipZ()) return false;
	if(c.y - radius > CDraw::GetFarClipZ()) return false;
	if(c.x*m_vecFrustumNormals[0].x + c.y*m_vecFrustumNormals[0].y > radius) return false;
	if(c.x*m_vecFrustumNormals[1].x + c.y*m_vecFrustumNormals[1].y > radius) return false;
	if(c.y*m_vecFrustumNormals[2].y + c.z*m_vecFrustumNormals[2].z > radius) return false;
	if(c.y*m_vecFrustumNormals[3].y + c.z*m_vecFrustumNormals[3].z > radius) return false;
	return true;
}

void
CCamera::SetRwCamera(rw::Camera *rwcam)
{
	m_pRwCamera = rwcam;
	m_viewMatrix.Attach(&rwcam->viewMatrix, false);
	// TODO: motion blur
}

void
CCamera::CalculateDerivedValues(void)
{
	m_cameraMatrix = Invert(GetMatrix());

	float hfov = DEGTORAD(CDraw::GetFOV()/2.0f);
	float c = cos(hfov);
	float s = sin(hfov);

	// right plane
	m_vecFrustumNormals[0] = CVector(c, -s, 0.0f);
	// left plane
	m_vecFrustumNormals[1] = CVector(-c, -s, 0.0f);

	c *= (float)globals.height/globals.width;
	s *= (float)globals.height/globals.width;
	// bottom plane
	m_vecFrustumNormals[2] = CVector(0.0f, -s, -c);
	// top plane
	m_vecFrustumNormals[3] = CVector(0.0f, -s, c);

	if(GetForward().x != 0.0f || GetForward().y != 0.0f)
		Orientation = atan2(GetForward().x, GetForward().y);
	else
		GetForward().x = 0.0001f;

	CamFrontXNorm = GetForward().x;
	CamFrontYNorm = GetForward().y;
	float l = sqrt(sq(CamFrontXNorm) + sq(CamFrontYNorm));
	if(l != 0.0f){
		CamFrontXNorm /= l;
		CamFrontYNorm /= l;
	}else
		CamFrontXNorm = 1.0f;
}

void
CCamera::Process(void)
{
	Scene.camera->setNearPlane(0.9f);

	Cams[ActiveCam].Process();

	// Update CCamera matrix with CCam values
	GetPosition() = Cams[ActiveCam].Source;
	GetForward() = Cams[ActiveCam].Front;
	GetUp() = Cams[ActiveCam].Up;
	GetRight() = CrossProduct(Cams[ActiveCam].Up, Cams[ActiveCam].Front);
	CalculateDerivedValues();	// BUG: this is stupid here because the FOV isn't set yet
	CDraw::SetFOV(Cams[ActiveCam].FOV);

	// Copy CCamera matrix to RW matrix
	rw::Matrix *camMatrix = &m_pRwCamera->getFrame()->matrix;
	camMatrix->pos = *(rw::V3d*)&GetPosition();
	camMatrix->at = *(rw::V3d*)&GetForward();
	camMatrix->up = *(rw::V3d*)&GetUp();
	camMatrix->right = *(rw::V3d*)&GetRight();
	camMatrix->update();
	m_pRwCamera->getFrame()->updateObjects();

	CDraw::SetNearClipZ(m_pRwCamera->nearPlane);
	CDraw::SetFarClipZ(m_pRwCamera->farPlane);
}

void
CCam::Init(void)
{
	Front = CVector(0.0f, 0.0f, -1.0f);
	Up = CVector(0.0f, 0.0f, 1.0f);
}

void
CCam::GetVectorsReadyForRW(void)
{
	CVector right;
	Up = CVector(0.0f, 0.0f, 1.0f);
	Front.Normalise();
	if(Front.x == 0.0f && Front.y == 0.0f){
		Front.x = 0.0001f;
		Front.y = 0.0001f;
	}
	right = CrossProduct(Front, Up);
	right.Normalise();
	Up = CrossProduct(right, Front);
}

void
CCamera::InitialiseCameraForDebugMode(void)
{
	CVector nfront = Cams[ActiveCam].Front;
	float groundDist = sqrt(sq(nfront.x) + sq(nfront.y));
	Cams[2].Beta = 0.0f; //CGeneral::GetATanOfXY(nfront.x, nfront.y);
	Cams[2].Alpha = 0.0f; //CGeneral::GetATanOfXY(groundDist, nfront.z);
	while(Cams[2].Beta >= PI) Cams[2].Beta -= 2.0f*PI;
	while(Cams[2].Beta < -PI) Cams[2].Beta += 2.0f*PI;
	while(Cams[2].Alpha >= PI) Cams[2].Alpha -= 2.0f*PI;
	while(Cams[2].Alpha < -PI) Cams[2].Alpha += 2.0f*PI;
}

void
CCam::Process_Debug(void)
{
	static float speed = 0.0f;
	static float panspeedX = 0.0f;
	static float panspeedY = 0.0f;

	float scale = CTimer::avgTimeStep/1000.0f*30.0f;
	CPad *pad = CPad::GetPad(0);

	FOV = 80.0f;

	Alpha += DEGTORAD(1.0f)/50.0f * pad->NewState.getLeftY()*128.0f * scale;
	Beta -= DEGTORAD(1.5f)/19.0f * pad->NewState.getLeftX()*128.0f * scale;

	if(Alpha > DEGTORAD(89.5f)) Alpha = DEGTORAD(89.5f);
	if(Alpha < DEGTORAD(-89.5f)) Alpha = DEGTORAD(-89.5f);

	CVector vec;
	vec.x = Source.x + cos(Beta) * cos(Alpha) * 3.0f;
	vec.y = Source.y + sin(Beta) * cos(Alpha) * 3.0f;
	vec.z = Source.z + sin(Alpha) * 3.0f;

	if(pad->NewState.square)
		speed += 0.1f;
	else if(pad->NewState.cross)
		speed -= 0.1f;
	else
		speed = 0.0f;

	Front = vec - Source;
	Front.Normalise();
	Source = Source + Front*speed;

	CVector up = { 0.0f, 0.0f, 1.0f };
	CVector right;
	right = CrossProduct(Front, up);
	up = CrossProduct(right, Front);
	Source = Source + up*panspeedY + right*panspeedX;

	GetVectorsReadyForRW();
}

void
CCam::Process(void)
{
	Process_Debug();
}
