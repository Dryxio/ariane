struct CSphere
{
	rw::V3d center;
	float radius;
};

struct CBox
{
	rw::V3d min;
	rw::V3d max;

	void FindMinMax(void){
		float tmp;
		if(max.x < min.x){
			tmp = min.x; min.x = max.x; max.x = tmp;
		}
		if(max.y < min.y){
			tmp = min.y; min.y = max.y; max.y = tmp;
		}
		if(max.z < min.z){
			tmp = min.z; min.z = max.z; max.z = tmp;
		}
	}
};

struct CompressedVector
{
	int16 x, y, z;
	rw::V3d Uncompress(void) {
		rw::V3d v = { x/128.0f, y/128.0f, z/128.0f };
		return v;
	}
};

struct CColSphere
{
	CSphere sph;
	uint8 surface;
	uint8 piece;
	uint8 lighting;

	void Set(float radius, rw::V3d *center, uint8 surf, uint8 piece, uint8 lighting){
		this->sph.center = *center;
		this->sph.radius = radius;
		this->surface = surf;
		this->piece = piece;
		this->lighting = lighting;
	}
};

struct CColBox
{
	CBox box;
	uint8 surface;
	uint8 piece;
	uint8 lighting;

	void Set(rw::V3d *min, rw::V3d *max, uint8 surf, uint8 piece, uint8 lighting){
		this->box.min = *min;
		this->box.max = *max;
		this->surface = surf;
		this->piece = piece;
		this->lighting = lighting;
	}
};

struct CColLine
{
	rw::V3d p0;
	int pad0;
	rw::V3d p1;
	int pad1;

	void Set(rw::V3d *p0, rw::V3d *p1){
		this->p0 = *p0;
		this->p1 =*p1;
	}
};

struct CColTriangle
{
	short a;
	short b;
	short c;
	uint8 surface;
	uint8 lighting;

	void Set(int a, int b, int c, uint8 surf, uint8 lighting){
		this->a = a;
		this->b = b;
		this->c = c;
		this->surface = surf;
		this->lighting = lighting;
	}
};

//struct CColPoint
//{
//	V3d point;
//	int pad1;
//	V3d normal;
//	int pad2;
//	uint8 surfaceTypeA;
//	uint8 pieceTypeA;
//	uint8 pieceTypeB;
//	uint8 surfaceTypeB;
//	float depth;
//};

struct GameFile;

struct CColModel
{
	CSphere boundingSphere;
	CBox boundingBox;

	short numSpheres;
	short numLines;
	short numBoxes;
	short numTriangles;
	int level;
	//uint8 unk1;
	CColSphere *spheres;
	CColLine *lines;
	CColBox *boxes;
	union {
		rw::V3d *vertices;
		CompressedVector *compVertices;
	};
	CColTriangle *triangles;
	//int unk2;

	uint8 flags;
	uint8 allocFlag;
	uint8 *rawdata;

	char name[24];
	GameFile *file;

	CColModel(void);
	~CColModel(void);
};

void ReadColModel(CColModel *colmodel, uint8 *buf, int size);
void ReadColModelVer2(CColModel *colmodel, uint8 *buf, int size);
void ReadColModelVer3(CColModel *colmodel, uint8 *buf, int size);
void ReadColModelVer4(CColModel *colmodel, uint8 *buf, int size);

void RenderColModelWire(CColModel *col, rw::Matrix *xform, bool onlyBounds);
