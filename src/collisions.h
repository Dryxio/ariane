struct CColSphere
{
	rw::V3d center;
	float radius;
	rw::uint8 surface;
	rw::uint8 piece;

	void Set(float radius, rw::V3d *center, rw::uint8 surf, rw::uint8 piece);
};

struct CColBox
{
	rw::V3d min;
	rw::V3d max;
	rw::uint8 surface;
	rw::uint8 piece;

	void Set(rw::V3d *min, rw::V3d *max, rw::uint8 surf, rw::uint8 piece);
};

struct CColLine
{
	rw::V3d p0;
	int pad0;
	rw::V3d p1;
	int pad1;

	void Set(rw::V3d *p0, rw::V3d *p1);
};

struct CColTriangle
{
	short a;
	short b;
	short c;
	rw::uint8 surface;

	void Set(int a, int b, int c, rw::uint8 surf);
};

//struct CColPoint
//{
//	rw::V3d point;
//	int pad1;
//	rw::V3d normal;
//	int pad2;
//	rw::uint8 surfaceTypeA;
//	rw::uint8 pieceTypeA;
//	rw::uint8 pieceTypeB;
//	rw::uint8 surfaceTypeB;
//	float depth;
//};

struct CColModel
{
	CColSphere boundingSphere;
	CColBox boundingBox;
	short numSpheres;
	short numLines;
	short numBoxes;
	short numTriangles;
	int level;
	//rw::uint8 unk1;
	CColSphere *spheres;
	CColLine *lines;
	CColBox *boxes;
	rw::V3d *vertices;
	CColTriangle *triangles;
	//int unk2;

	CColModel(void);
	~CColModel(void);
};

void readColModel(CColModel *colmodel, rw::uint8 *buf);
rw::uint32 writeColModel(CColModel *colmodel, rw::uint8 **buf);
