class CCamera
{
public:
	rw::Camera *m_rwcam;
	rw::V3d m_position;
	rw::V3d m_target;
	rw::V3d m_up;
	rw::V3d m_localup;

	float m_fov, m_aspectRatio;

	float m_LODmult;

	void Process(void);
	void DrawTarget(void);

	void setTarget(rw::V3d target);
	float getHeading(void);

	void turn(float yaw, float pitch);
	void orbit(float yaw, float pitch);
	void dolly(float dist);
	void zoom(float dist);
	void pan(float x, float y);
	void setDistanceFromTarget(float dist);

	void update(void);
	float distanceTo(rw::V3d v);
	float distanceToTarget(void);
	float minDistToSphere(float r);
	CCamera(void);

	bool IsSphereVisible(rw::Sphere *sph, rw::Matrix *xform);
};
