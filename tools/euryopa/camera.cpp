#include "euryopa.h"

#define PI 3.14159265359f
#ifndef min
#define min(a, b) (a < b ? a : b)
#endif

CCamera TheCamera;

using rw::Quat;
using rw::V3d;

void
CCamera::Process(void)
{
	float scl = avgTimeStep*30.0f;
	float sensitivity = 1.0f;

	ImGuiIO &io = ImGui::GetIO();
	bool blockMouse = io.WantCaptureMouse || gGizmoHovered || gGizmoUsing;

	this->mx = CPad::newMouseState.x/io.DisplaySize.x;
	this->my = CPad::newMouseState.y/io.DisplaySize.y;
	this->mx = this->mx*2.0f - 1.0f;
	this->my = this->my*2.0f - 1.0f;

	using namespace rw;
	V3d dir = normalize(sub(TheCamera.m_target, TheCamera.m_position));
	V3d right = scale(normalize(cross(dir, TheCamera.m_up)), m_rwcam->viewWindow.x);
	V3d localup = scale(normalize(cross(right, dir)), m_rwcam->viewWindow.y);
	m_mouseDir = add(scale(right, this->mx), scale(localup, -this->my));
	m_mouseDir = add(m_mouseDir, m_at);
//	m_mouseDir = normalize(m_mouseDir);

	// Mouse
	// first person (LMB outside brush mode; RMB-drag while brushing so LMB stays free for painting)
	bool lookLmb = !gBrushMode && !gRectSelectActive && CPad::IsMButtonDown(1);
	bool lookRmb = gBrushMode && CPad::IsMButtonDown(3);
	if(!blockMouse && (lookLmb || lookRmb)){
		if(CPad::IsAltDown() && CPad::IsCtrlDown()){
			float dy = (CPad::oldMouseState.y - CPad::newMouseState.y);
			dolly(dy*scl);
		}else{
			float dx = (CPad::oldMouseState.x - CPad::newMouseState.x);
			float dy = (CPad::oldMouseState.y - CPad::newMouseState.y);
			turn(DEGTORAD(dx)/2.0f*scl, DEGTORAD(dy)/2.0f*scl);
		}
	}
	// roughly 3ds max controls
	if(!blockMouse && CPad::IsMButtonDown(2)){
		if(CPad::IsAltDown() && CPad::IsCtrlDown()){
			float dy = (CPad::oldMouseState.y - CPad::newMouseState.y);
			zoom(dy*scl);
		}else if(CPad::IsAltDown()){
			float dx = (CPad::oldMouseState.x - CPad::newMouseState.x);
			float dy = (CPad::oldMouseState.y - CPad::newMouseState.y);
			orbit(DEGTORAD(dx)/2.0f*scl, -DEGTORAD(dy)/2.0f*scl);
		}else{
			float dx = (CPad::oldMouseState.x - CPad::newMouseState.x);
			float dy = (CPad::oldMouseState.y - CPad::newMouseState.y);
			float dist = distanceToTarget();
			pan(dx*scl*dist/100.0f, -dy*scl*dist/100.0f);
		}
	}

	// Keyboard
	float flyMul = 1.0f;
	if(CPad::IsShiftDown()) flyMul = gFlyFastMul;
	else if(CPad::IsAltDown()) flyMul = gFlySlowMul;

	static float speed = 0.0f;
	if(CPad::IsKeyDown('W'))
		speed += 0.1f;
	else if(CPad::IsKeyDown('S'))
		speed -= 0.1f;
	else
		speed = 0.0f;
	if(speed > 70.0f) speed = 70.0f;
	if(speed < -70.0f) speed = -70.0f;
	dolly(speed*scl*flyMul);

	static float sidespeed = 0.0f;
	if(CPad::IsKeyDown('A'))
		sidespeed -= 0.1f;
	else if(CPad::IsKeyDown('D'))
		sidespeed += 0.1f;
	else
		sidespeed = 0.0f;
	if(sidespeed > 70.0f) sidespeed = 70.0f;
	if(sidespeed < -70.0f) sidespeed = -70.0f;
	pan(sidespeed*scl*flyMul, 0.0f);



	// Pad
	CPad *pad = CPad::GetPad(0);
	sensitivity = 1.0f;
	if(pad->NewState.r2){
		sensitivity = 2.0f;
		if(pad->NewState.l2)
			sensitivity = 4.0f;
	}else if(pad->NewState.l2)
		sensitivity = 0.5f;
	if(pad->NewState.square) zoom(0.4f*sensitivity*scl);
	if(pad->NewState.cross) zoom(-0.4f*sensitivity*scl);
	orbit(pad->NewState.getLeftX()/25.0f*sensitivity*scl,
	                -pad->NewState.getLeftY()/25.0f*sensitivity*scl);
	turn(-pad->NewState.getRightX()/25.0f*sensitivity*scl,
	               pad->NewState.getRightY()/25.0f*sensitivity*scl);
	if(pad->NewState.up)
		dolly(2.0f*sensitivity*scl);
	if(pad->NewState.down)
		dolly(-2.0f*sensitivity*scl);

//	if(IsButtonJustDown(pad, start)){
//		printf("cam.position: %f, %f, %f\n", m_position.x, m_position.y, m_position.z);
//		printf("cam.target: %f, %f, %f\n", m_target.x, m_target.y, m_target.z);
//	}
}

void
CCamera::DrawTarget(void)
{
	float dist = distanceToTarget()/20.0f;
	rw::V3d x = { dist, 0.0f, 0.0f };
	rw::V3d y = { 0.0f, dist, 0.0f };
	rw::V3d z = { 0.0f, 0.0f, dist };
	RenderAxesWidget(this->m_target, x, y, z);
}

void
CCamera::update(void)
{
	if(m_rwcam){
		m_rwcam->setFOV(m_fov, m_aspectRatio);
		m_rwcam_viewer->setFOV(m_fov, m_aspectRatio);

		rw::Frame *f = m_rwcam->getFrame();
		if(f){
			rw::V3d dir = sub(m_target, m_position);
			if(length(dir) < 0.0001f)
				dir = m_at;
			m_at = normalize(dir);
			f->matrix.lookAt(m_at, m_up);
			f->matrix.pos = m_position;
			f->updateObjects();
		}
	}
}

void
CCamera::setTarget(V3d target)
{
	m_position = sub(m_position, sub(m_target, target));
	m_target = target;
}

float
CCamera::getHeading(void)
{
	V3d dir = sub(m_target, m_position);
	float a = atan2(dir.y, dir.x)-PI/2.0f;
	return m_localup.z < 0.0f ? a-PI : a;
}

void
CCamera::turn(float yaw, float pitch)
{
	V3d dir = sub(m_target, m_position);
	V3d zaxis = { 0.0f, 0.0f, 1.0f };
	Quat r = Quat::rotation(yaw, zaxis);
	dir = rotate(dir, r);
	m_localup = rotate(m_localup, r);

	V3d right = normalize(cross(dir, m_localup));
	r = Quat::rotation(pitch, right);
	dir = rotate(dir, r);
	m_localup = normalize(cross(right, dir));
	if(m_localup.z >= 0.0) m_up.z = 1.0;
	else m_up.z = -1.0f;

	m_target = add(m_position, dir);
}

void
CCamera::orbit(float yaw, float pitch)
{
	V3d dir = sub(m_target, m_position);
	V3d zaxis = { 0.0f, 0.0f, 1.0f };
	Quat r = Quat::rotation(yaw, zaxis);
	dir = rotate(dir, r);
	m_localup = rotate(m_localup, r);

	V3d right = normalize(cross(dir, m_localup));
	r = Quat::rotation(-pitch, right);
	dir = rotate(dir, r);
	m_localup = normalize(cross(right, dir));
	if(m_localup.z >= 0.0) m_up.z = 1.0;
	else m_up.z = -1.0f;

	m_position = sub(m_target, dir);
}

void
CCamera::dolly(float dist)
{
	V3d dir = setlength(sub(m_target, m_position), dist);
	m_position = add(m_position, dir);
	m_target = add(m_target, dir);
}

void
CCamera::zoom(float dist)
{
	V3d dir = sub(m_target, m_position);
	float curdist = length(dir);
	if(dist >= curdist)
		dist = curdist-0.3f;
	dir = setlength(dir, dist);
	m_position = add(m_position, dir);
}

void
CCamera::pan(float x, float y)
{
	V3d dir = normalize(sub(m_target, m_position));
	V3d right = normalize(cross(dir, m_up));
	V3d localup = normalize(cross(right, dir));
	dir = add(scale(right, x), scale(localup, y));
	m_position = add(m_position, dir);
	m_target = add(m_target, dir);
}

void
CCamera::setDistanceFromTarget(float dist)
{
	V3d dir = sub(m_position, m_target);
	dir = scale(normalize(dir), dist);
	m_position = add(m_target, dir);
}

void
CCamera::setDistanceToTarget(float dist)
{
	V3d dir = sub(m_target, m_position);
	dir = scale(normalize(dir), dist);
	m_target = add(m_position, dir);
}

float
CCamera::distanceTo(V3d v)
{
	return length(sub(m_position, v));
}

float
CCamera::distanceToTarget(void)
{
	return length(sub(m_position, m_target));
}

// calculate minimum distance to a sphere at the target
// so the whole sphere is visible
float
CCamera::minDistToSphere(float r)
{
	float t = min(m_rwcam->viewWindow.x, m_rwcam->viewWindow.y);
	float a = atan(t);	// half FOV angle
	return r/sin(a);
}

CCamera::CCamera()
{
	m_position.set(0.0f, 6.0f, 0.0f);
	m_target.set(0.0f, 0.0f, 0.0f);

	m_up.set(0.0f, 0.0f, 1.0f);
	m_localup = m_up;
	m_fov = 70.0f;
	m_aspectRatio = 1.0f;
	m_rwcam = nil;
	m_LODmult = 1.0f;
}


bool
CCamera::IsSphereVisible(rw::Sphere *sph, rw::Matrix *xform)
{
	rw::Sphere sphere = *sph;
	rw::Camera *cam = gUseViewerCam && m_rwcam_viewer ? m_rwcam_viewer : m_rwcam;
	rw::V3d::transformPoints(&sphere.center, &sphere.center, 1, xform);
	return cam->frustumTestSphere(&sphere) != rw::Camera::SPHEREOUTSIDE;
}
