#ifndef _BASICTYPES_H_
#define _BASICTYPES_H_

inline float clamp(float f) { return f > 1.0f ? 1.0f : f; }

class CVector
{
public:
	float x, y, z;
	CVector(void) {}
	CVector(float x, float y, float z) : x(x), y(y), z(z) {}
	CVector(rw::V3d const &v) : x(v.x), y(v.y), z(v.z) {}
	float Magnitude(void) const { return sqrt(x*x + y*y + z*z); }
	float MagnitudeSqr(void) const { return x*x + y*y + z*z; }
	float Magnitude2D(void) const { return sqrt(x*x + y*y); }
	void Normalise(void){
		float sq = MagnitudeSqr();
		if(sq > 0.0f){
			float invsqrt = 1.0f/sqrt(sq);
			x *= invsqrt;
			y *= invsqrt;
			z *= invsqrt;
		}else
			x = 1.0f;
	}
	rw::V3d ToRW(void){
		return rw::makeV3d(x, y, z);
	}
	void operator=(rw::V3d const &rhs){
		x = rhs.x;
		y = rhs.y;
		z = rhs.z;
	}
	CVector operator-(const CVector &rhs) const {
		return CVector(x-rhs.x, y-rhs.y, z-rhs.z);
	}
	CVector operator+(const CVector &rhs) const {
		return CVector(x+rhs.x, y+rhs.y, z+rhs.z);
	}
	CVector operator*(float t) const {
		return CVector(x*t, y*t, z*t);
	}
	CVector operator/(float t) const {
		return CVector(x/t, y/t, z/t);
	}
	CVector &operator-=(const CVector &rhs) {
		this->x -= rhs.x;
		this->y -= rhs.y;
		this->z -= rhs.z;
		return *this;
	}
	CVector &operator+=(const CVector &rhs) {
		this->x += rhs.x;
		this->y += rhs.y;
		this->z += rhs.z;
		return *this;
	}
	CVector &operator*=(float t) {
		this->x *= t;
		this->y *= t;
		this->z *= t;
		return *this;
	}
	CVector &operator/=(float t) {
		this->x /= t;
		this->y /= t;
		this->z /= t;
		return *this;
	}
};

class CVector2D
{
public:
	float x, y;
	CVector2D(void) {}
	CVector2D(float x, float y) : x(x), y(y) {}
	CVector2D(const CVector &v) : x(v.x), y(v.y) {}
	float Magnitude(void) const { return sqrt(x*x + y*y); }
	float MagnitudeSqr(void) const { return x*x + y*y; }

	void Normalise(void){
		float sq = MagnitudeSqr();
		if(sq > 0.0f){
			float invsqrt = 1.0f/sqrt(sq);
			x *= invsqrt;
			y *= invsqrt;
		}else
			x = 0.0f;
	}
	CVector2D operator-(const CVector2D &rhs) const {
		return CVector2D(x-rhs.x, y-rhs.y);
	}
	CVector2D operator+(const CVector2D &rhs) const {
		return CVector2D(x+rhs.x, y+rhs.y);
	}
	CVector2D operator*(float t) const {
		return CVector2D(x*t, y*t);
	}
};

inline float
DotProduct(const CVector &v1, const CVector &v2)
{
	return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

inline CVector
CrossProduct(const CVector &v1, const CVector &v2)
{
	return CVector(v1.y*v2.z - v1.z*v2.y,
		v1.z*v2.x - v1.x*v2.z,
		v1.x*v2.y - v1.y*v2.x);
}

inline float
CrossProduct2D(const CVector2D &v1, const CVector2D &v2)
{
	return v1.x*v2.y - v1.y*v2.x;
}

class CMatrix
{
public:
	rw::Matrix m_matrix;
	rw::Matrix *m_attachment;
	bool m_hasRwMatrix;	// are we the owner?

	CMatrix(void){
		m_attachment = nil;
		m_hasRwMatrix = false;
	}
	CMatrix(CMatrix const &m){
		m_attachment = nil;
		m_hasRwMatrix = false;
		*this = m;
	}
	CMatrix(rw::Matrix *matrix, bool attach){
		m_attachment = nil;
		Attach(matrix, attach);
	}
	~CMatrix(void){
		if(m_hasRwMatrix && m_attachment)
			m_attachment->destroy();
	}
	void Attach(rw::Matrix *matrix, bool attach){
		if(m_hasRwMatrix && m_attachment)
			m_attachment->destroy();
		m_attachment = matrix;
		m_hasRwMatrix = attach;
		Update();
	}
	void AttachRW(rw::Matrix *matrix, bool attach){
		if(m_hasRwMatrix && m_attachment)
			m_attachment->destroy();
		m_attachment = matrix;
		m_hasRwMatrix = attach;
		UpdateRW();
	}
	void Detach(void){
		if(m_hasRwMatrix && m_attachment)
			m_attachment->destroy();
		m_attachment = nil;
	}
	void Update(void){
		m_matrix = *m_attachment;
	}
	void UpdateRW(void){
		if(m_attachment){
			*m_attachment = m_matrix;
			m_attachment->update();
		}
	}
	void operator=(CMatrix const &rhs){
		m_matrix = rhs.m_matrix;
		if(m_attachment)
			UpdateRW();
	}

	CVector *GetPosition(void){ return (CVector*)&m_matrix.pos; }
	CVector *GetRight(void) { return (CVector*)&m_matrix.right; }
	CVector *GetForward(void) { return (CVector*)&m_matrix.up; }
	CVector *GetUp(void) { return (CVector*)&m_matrix.at; }
	void SetScale(float s){
		m_matrix.right.x = s;
		m_matrix.right.y = 0.0f;
		m_matrix.right.z = 0.0f;

		m_matrix.up.x = 0.0f;
		m_matrix.up.y = s;
		m_matrix.up.z = 0.0f;

		m_matrix.at.x = 0.0f;
		m_matrix.at.y = 0.0f;
		m_matrix.at.z = s;

		m_matrix.pos.x = 0.0f;
		m_matrix.pos.y = 0.0f;
		m_matrix.pos.z = 0.0f;
	}
	void Reorthogonalise(void){
		CVector &r = *GetRight();
		CVector &f = *GetForward();
		CVector &u = *GetUp();
		u = CrossProduct(r, f);
		u.Normalise();
		r = CrossProduct(f, u);
		r.Normalise();
		f = CrossProduct(u, r);
	}
};

inline CMatrix&
Invert(const CMatrix &src, CMatrix &dst)
{
	// GTA handles this as a raw 4x4 orthonormal matrix
	// and trashes the RW flags, let's not do that
	// actual copy of librw code:
	rw::Matrix *d = &dst.m_matrix;
	const rw::Matrix *s = &src.m_matrix;
	d->right.x = s->right.x;
	d->right.y = s->up.x;
	d->right.z = s->at.x;
	d->up.x = s->right.y;
	d->up.y = s->up.y;
	d->up.z = s->at.y;
	d->at.x = s->right.z;
	d->at.y = s->up.z;
	d->at.z = s->at.z;
	d->pos.x = -(s->pos.x*s->right.x +
	               s->pos.y*s->right.y +
	               s->pos.z*s->right.z);
	d->pos.y = -(s->pos.x*s->up.x +
	               s->pos.y*s->up.y +
	               s->pos.z*s->up.z);
	d->pos.z = -(s->pos.x*s->at.x +
	               s->pos.y*s->at.y +
	               s->pos.z*s->at.z);
	d->flags = rw::Matrix::TYPEORTHONORMAL;
	return dst;
}

inline CMatrix
Invert(const CMatrix &matrix)
{
	CMatrix inv;
	return Invert(matrix, inv);
}

inline CVector
operator*(const CMatrix &mat, const CVector &vec)
{
	return CVector(
		mat.m_matrix.right.x * vec.x + mat.m_matrix.up.x * vec.y + mat.m_matrix.at.x * vec.z + mat.m_matrix.pos.x,
		mat.m_matrix.right.y * vec.x + mat.m_matrix.up.y * vec.y + mat.m_matrix.at.y * vec.z + mat.m_matrix.pos.y,
		mat.m_matrix.right.z * vec.x + mat.m_matrix.up.z * vec.y + mat.m_matrix.at.z * vec.z + mat.m_matrix.pos.z);
}

inline CMatrix
operator*(const CMatrix &m1, const CMatrix &m2)
{
	CMatrix out;
	rw::Matrix *dst = &out.m_matrix;
	const rw::Matrix *src1 = &m1.m_matrix;
	const rw::Matrix *src2 = &m2.m_matrix;
	dst->right.x = src1->right.x*src2->right.x + src1->up.x*src2->right.y + src1->at.x*src2->right.z;
	dst->right.y = src1->right.y*src2->right.x + src1->up.y*src2->right.y + src1->at.y*src2->right.z;
	dst->right.z = src1->right.z*src2->right.x + src1->up.z*src2->right.y + src1->at.z*src2->right.z;
	dst->up.x    = src1->right.x*src2->up.x    + src1->up.x*src2->up.y    + src1->at.x*src2->up.z;
	dst->up.y    = src1->right.y*src2->up.x    + src1->up.y*src2->up.y    + src1->at.y*src2->up.z;
	dst->up.z    = src1->right.z*src2->up.x    + src1->up.z*src2->up.y    + src1->at.z*src2->up.z;
	dst->at.x    = src1->right.x*src2->at.x    + src1->up.x*src2->at.y    + src1->at.x*src2->at.z;
	dst->at.y    = src1->right.y*src2->at.x    + src1->up.y*src2->at.y    + src1->at.y*src2->at.z;
	dst->at.z    = src1->right.z*src2->at.x    + src1->up.z*src2->at.y    + src1->at.z*src2->at.z;
	dst->pos.x   = src1->right.x*src2->pos.x   + src1->up.x*src2->pos.y   + src1->at.x*src2->pos.z + src1->pos.x;
	dst->pos.y   = src1->right.y*src2->pos.x   + src1->up.y*src2->pos.y   + src1->at.y*src2->pos.z + src1->pos.y;
	dst->pos.z   = src1->right.z*src2->pos.x   + src1->up.z*src2->pos.y   + src1->at.z*src2->pos.z + src1->pos.z;
	return out;
}

inline CVector
MultiplyInverse(const CMatrix &mat, const CVector &vec)
{
	CVector v(vec.x - mat.m_matrix.pos.x, vec.y - mat.m_matrix.pos.y, vec.z - mat.m_matrix.pos.z);
	return CVector(
		mat.m_matrix.right.x * v.x + mat.m_matrix.right.y * v.y + mat.m_matrix.right.z * v.z,
		mat.m_matrix.up.x * v.x + mat.m_matrix.up.y * v.y + mat.m_matrix.up.z * v.z,
		mat.m_matrix.at.x * v.x + mat.m_matrix.at.y * v.y + mat.m_matrix.at.z * v.z);
}

inline CVector
Multiply3x3(const CMatrix &mat, const CVector &vec)
{
	return CVector(
		mat.m_matrix.right.x * vec.x + mat.m_matrix.up.x * vec.y + mat.m_matrix.at.x * vec.z,
		mat.m_matrix.right.y * vec.x + mat.m_matrix.up.y * vec.y + mat.m_matrix.at.y * vec.z,
		mat.m_matrix.right.z * vec.x + mat.m_matrix.up.z * vec.y + mat.m_matrix.at.z * vec.z);
}

class CRGBA
{
public:
	uint8 r, g, b, a;
	CRGBA(void) { }
	CRGBA(uint8 r, uint8 g, uint8 b, uint8 a) : r(r), g(g), b(b), a(a) { }
};

#endif
