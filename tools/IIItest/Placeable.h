#ifndef _PLACEABLE_H_
#define _PLACEABLE_H_

class CPlaceable
{
	// disable allocation
	static void *operator new(size_t) { assert(0); return nil; }
	static void operator delete(void*, size_t) { assert(0); }
public:
	CMatrix m_matrix;

	CPlaceable(void);
	virtual ~CPlaceable(void);
	CVector &GetPosition(void) { return *m_matrix.GetPosition(); }
	CVector &GetRight(void) { return *m_matrix.GetRight(); }
	CVector &GetForward(void) { return *m_matrix.GetForward(); }
	CVector &GetUp(void) { return *m_matrix.GetUp(); }
	CMatrix &GetMatrix(void) { return m_matrix; }
	void SetTransform(rw::Matrix *m) { m_matrix = CMatrix(m, false); }
};

#endif
