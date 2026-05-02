#ifndef _CAMERA_H_
#define _CAMERA_H_

class CCam
{
public:
	float Alpha;
	float FOV;	
	float Beta;
	CVector Front;
	CVector Source;
	CVector Up;
	float m_fCameraDistance;

	void Init(void);
	void Process(void);
	void Process_Debug(void);
	void GetVectorsReadyForRW(void);
};

class CCamera : public CPlaceable
{
	// disable allocation
	static void *operator new(size_t) { assert(0); return nil; }
	static void operator delete(void*, size_t) { assert(0); }
public:
	uint8 ActiveCam;
	float CamFrontXNorm;
	float CamFrontYNorm;
	float LODDistMultiplier;
	float Orientation;
	CCam Cams[3];
	rw::Camera *m_pRwCamera;
	CMatrix m_cameraMatrix;
	CMatrix m_viewMatrix;
	CVector m_vecFrustumNormals[4];	// frustum normals for left, right, bottom, top planes in camera space

	void Init(void);
	void Process(void);

	CMatrix &GetCameraMatrix(void) { return m_cameraMatrix; }

	void SetRwCamera(rw::Camera *rwcam);
	void CalculateDerivedValues(void);
	bool IsSphereVisible(const CVector &center, float radius);
	void InitialiseCameraForDebugMode(void);
};

extern CCamera TheCamera;

#endif
