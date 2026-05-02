#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

using namespace std;
using namespace rw;

static struct {
	const char *str;
	uint32 val;
} platforms[] = {
	{ "mobile", PLATFORM_WDGL },
	{ "ps2",    PLATFORM_PS2 },
	{ "xbox",   PLATFORM_XBOX },
	{ "d3d8",   PLATFORM_D3D8 },
	{ "d3d9",   PLATFORM_D3D9 },
	{ NULL, 0 }
};

char *argv0;
char *inputfilename;

void dumpjson(Clump*, const char *file);
void centerTexCoords(Geometry *g);

void
usage(void)
{
	fprintf(stderr, "usage: %s [-u] [-i] [-f] [-d] [-v version] [-o platform] in.dff [out.dff]\n", argv0);
	fprintf(stderr, "\t-u uninstance\n");
	fprintf(stderr, "\t-i instance\n");
	fprintf(stderr, "\t-f don't flip frame hierarchy\n");
	fprintf(stderr, "\t-d dump frame and hanim hierarchy\n");
	fprintf(stderr, "\t-w correct face winding of tristrips\n");
	fprintf(stderr, "\t-s make output tristrips\n");
	fprintf(stderr, "\t-m extract multiclump dff\n");
	fprintf(stderr, "\t-v RW version, e.g. 33004 for 3.3.0.4\n");
	fprintf(stderr, "\t-o output platform. ps2, xbox, mobile, d3d8, d3d9\n");
	fprintf(stderr, "\t-b remove textures containing \"body\" on editable materials\n");
	fprintf(stderr, "\t-bb remove textures containing \"body\" on all materials\n");
	fprintf(stderr, "\t-a auto-assign hanim IDs (use when converting old skins. may fail)\n");
	fprintf(stderr, "\t--III2VCcar specmap. convert reflections from III to VC using specmap\n");
	fprintf(stderr, "\t--ps2VCcar set up VC vehicle for use with PS2 and Xbox reflections\n");
	fprintf(stderr, "\t--xboxbuilding convert SA Xbox building to something PS2 and PC understand\n");
	fprintf(stderr, "\t--ps2SAcar assign PS2 SA car pipelines to a model\n");
	fprintf(stderr, "\t--ps2SAbuilding assign PS2 SA building pipelines to a model\n");
	fprintf(stderr, "\t--ps2SAped assign PS2 SA ped pipeline to a model\n");
	fprintf(stderr, "\t--info dump some info about the file\n");
	exit(1);
}

// Convert normals to extra colors
// and extra normals to regular normals.
void
convertSAXboxBuilding(Atomic *atomic)
{
	uint32 id = gta::getPipelineID(atomic);
	if(id != gta::RSPIPE_XBOX_CustomBuilding_PipeID &&
	   id != gta::RSPIPE_XBOX_CustomBuildingDN_PipeID &&
	   id != gta::RSPIPE_XBOX_CustomBuildingEnvMap_PipeID &&
	   id != gta::RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID)
		return;

	Geometry *geo = atomic->geometry;

	// this shouldn't happen
	if((geo->flags & Geometry::PRELIT) == 0)
		return;

	// Normals to Extra colors
	// Copy alpha from normal colors (these will be wet road alpha now)
	if(geo->flags & Geometry::NORMALS){
		RGBA *daycols = geo->colors;
		if(gta::getExtraVertColors(atomic) == nil)
			gta::allocateExtraVertColors(geo);
		RGBA *nightcols = gta::getExtraVertColors(atomic);
		V3d *normals = geo->morphTargets[0].normals;

		int32 i;
		for(i = 0; i < geo->numVertices; i++){
			nightcols[i].red = normals[i].x*255;
			nightcols[i].green = normals[i].y*255;
			nightcols[i].blue = normals[i].z*255;
			nightcols[i].alpha = daycols[i].alpha;
		}
	}

	// Extra normals to normals
	V3d *extranormals = gta::getExtraNormals(geo);
	if(extranormals){
		if((geo->flags & Geometry::NORMALS) == 0){
			// Yikes! We have to allocate normals
			assert(geo->numMorphTargets == 1);
			// First save the old morph target from being deallocated
			MorphTarget *mt = geo->morphTargets;
			geo->morphTargets = nil;
			geo->numMorphTargets = 0;
			// Now allocate a new morph target with normals
			geo->flags |= Geometry::NORMALS;
			geo->addMorphTargets(1);
			// Move over data
			geo->morphTargets->boundingSphere = mt->boundingSphere;
			memcpy(geo->morphTargets->vertices, mt->vertices, geo->numVertices*sizeof(V3d));
			// finally free old data
			rwFree(mt);
		}
		// Just copy and free old data
		memcpy(geo->morphTargets->normals, extranormals, geo->numVertices*sizeof(V3d));
		gta::freeExtraNormals(geo);
	}else
		geo->flags &= ~Geometry::NORMALS;	// in case we had colors in the normals

	// Clear pipeline
	gta::setPipelineID(atomic, 0);
	atomic->pipeline = nil;
}

void
assignPS2SAcarPipes(Atomic *atomic)
{
	int i;
	Geometry *geo;
	Material *mat;
	gta::EnvMat *env;

	geo = atomic->geometry;
	if(geo->instData){
		fprintf(stderr, "error: can't re-assign pipeline on pre-instanced geometry\n");
		exit(1);
	}
	for(i = 0; i < geo->matList.numMaterials; i++){
		mat = geo->matList.materials[i];
		mat->pipeline = ps2::getPDSPipe(gta::PDS_PS2_CustomCar_MatPipeID);
		// This should correspond to the logic used by the game to assign pipes.
		// It's important to get this exactly right
		env = gta::getEnvMat(mat);
		if(env && env->shininess != 0 && MatFX::getEffects(mat) == MatFX::ENVMAP){
			Texture *envtex = MatFX::get(mat)->getEnvTexture();
			if(envtex){
				if(envtex->name[0] == 'x')
					mat->pipeline = ps2::getPDSPipe(gta::PDS_PS2_CustomCarEnvMapUV2_MatPipeID);
				else
					mat->pipeline = ps2::getPDSPipe(gta::PDS_PS2_CustomCarEnvMap_MatPipeID);
			}
		}
	}
	// Only obj pipe used by gta
	atomic->pipeline = (ObjPipeline*)ps2::getPDSPipe(gta::PDS_PS2_CustomCar_AtmPipeID);
}

void
assignPS2SAbuildingPipes(Atomic *atomic)
{
	int i;
	Geometry *geo;
	Material *mat;
	uint32 fx;

	geo = atomic->geometry;
	if(geo->instData){
		fprintf(stderr, "error: can't re-assign pipeline on pre-instanced geometry\n");
		exit(1);
	}

	// Don't give building pipe to skinned geometry
	if(atomic->pipeline && atomic->pipeline->pluginID == ID_SKIN)
		return;

	bool dn = gta::getExtraVertColors(atomic) != nil;

	for(i = 0; i < geo->matList.numMaterials; i++){
		mat = geo->matList.materials[i];

		fx = MatFX::getEffects(mat);
		if(fx == MatFX::UVTRANSFORM)
			mat->pipeline = ps2::getPDSPipe(dn ? gta::PDS_PS2_CustomBuildingDNUVA_MatPipeID : gta::PDS_PS2_CustomBuildingUVA_MatPipeID);
		else if(fx == MatFX::ENVMAP && MatFX::get(mat)->getEnvTexture() && geo->flags & Geometry::NORMALS)
			mat->pipeline = ps2::getPDSPipe(dn ? gta::PDS_PS2_CustomBuildingDNEnvMap_MatPipeID : gta::PDS_PS2_CustomBuildingEnvMap_MatPipeID);
		else
			mat->pipeline = ps2::getPDSPipe(dn ? gta::PDS_PS2_CustomBuildingDN_MatPipeID : gta::PDS_PS2_CustomBuilding_MatPipeID);
	}
	atomic->pipeline = (ObjPipeline*)ps2::getPDSPipe(dn ? gta::PDS_PS2_CustomBuildingDN_AtmPipeID : gta::PDS_PS2_CustomBuilding_AtmPipeID);
}

void
assignPS2SAPedPipe(Atomic *atomic)
{
	int i;
	Geometry *geo;
	Material *mat;

	geo = atomic->geometry;
	if(geo->instData){
		fprintf(stderr, "error: can't re-assign pipeline on pre-instanced geometry\n");
		exit(1);
	}

	if(atomic->pipeline && atomic->pipeline->pluginID == ID_SKIN)
		return;

	for(i = 0; i < geo->matList.numMaterials; i++){
		mat = geo->matList.materials[i];
		mat->pipeline = ps2::getPDSPipe(gta::PDS_PS2_CustomSkinPed_MatPipeID);
	}
	atomic->pipeline = (ObjPipeline*)ps2::getPDSPipe(gta::PDS_PS2_CustomSkinPed_AtmPipeID);
}

void
dumpUVAnim(Animation *anim)
{
	UVAnimCustomData *cust = (UVAnimCustomData*)anim->customData;
	printf(" %s", cust->name);
	for(int i = 0; i < 8; i++)
		printf(" %d", cust->nodeToUVChannel[i]);
	printf("\n %d %x\n", anim->numFrames, anim->interpInfo->id);
	UVAnimKeyFrame *kf = (UVAnimKeyFrame*)anim->keyframes;
	for(int i = 0; i < anim->numFrames; i++){
		printf(" %f\n", kf->time);
		printf("  %f %f %f %f %f %f\n", kf->uv[0], kf->uv[1], kf->uv[2], kf->uv[3], kf->uv[4], kf->uv[5]);
		kf++;
	}
}

char*
getFrameUserName(rw::Frame *f)
{
	using namespace rw;
	int32 i;
	UserDataArray *ar;
	int32 n = UserDataArray::frameGetCount(f);
	for(i = 0; i < n; i++){
		ar = UserDataArray::frameGet(f, i);
		if(strcmp(ar->name, "name") == 0 && ar->datatype == rw::USERDATASTRING)
			return ar->getString(0);
	}
	return (char*)"";
}

char*
getFrameName(Frame *f)
{
	char *name = gta::getNodeName(f);
	if(name[0] == '\0')
		name = getFrameUserName(f);
	return name;
}

void
printMatrix(Matrix *mat)
{
	printf("[ [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
	       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
	       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
	       "  [ %8.4f, %8.4f, %8.4f, %8.4f ] ]\n"
		"  %08x == flags\n",
		mat->right.x, mat->up.x, mat->at.x, mat->pos.x,
		mat->right.y, mat->up.y, mat->at.y, mat->pos.y,
		mat->right.z, mat->up.z, mat->at.z, mat->pos.z,
		0.0f, 0.0f, 0.0f, 1.0f,
		mat->flags);
}

void
dumpFrameHier(Frame *frame, int dumpMat, int ind = 0)
{
	for(int i = 0; i < ind; i++)
		printf("  ");
	const char *name = getFrameName(frame);
	HAnimData *hanim = HAnimData::get(frame);
	printf("*%s %d %d %s\n", name[0] ? name : "---", frame->objectList.count(), hanim->id, hanim->hierarchy ? "HIERARCHY" : "");
	if(hanim->hierarchy){
		HAnimHierarchy *h = hanim->hierarchy;
		for(int i = 0; i < h->numNodes; i++){
			name = h->nodeInfo[i].frame ? getFrameName(h->nodeInfo[i].frame) : "";
			printf("\t\t%d %d\t%p %s\n", h->nodeInfo[i].id, h->nodeInfo[i].flags, h->nodeInfo[i].frame, name);

			if(dumpMat && h->nodeInfo[i].frame){
				printMatrix(h->nodeInfo[i].frame->getLTM());
			//	printMatrix(&h->nodeInfo[i].frame->matrix);
			}
		}
	}
	//if(dumpMat && frame){
	//	printMatrix(frame->getLTM());
	//	printMatrix(&frame->matrix);
	//}
	for(Frame *child = frame->child;
	    child; child = child->next)
		dumpFrameHier(child, dumpMat, ind+1);
}

void
dumpBoneMatrices(Skin *skin, HAnimHierarchy *hier)
{
	int i;
	Matrix *mat;

	for(i = 0; i < skin->numBones; i++){
		Matrix inv;
		mat = (Matrix*)&skin->inverseMatrices[i*16];
		Matrix::invert(&inv, mat);
		mat = &inv;
		printf("node %s\n", gta::getNodeName(hier->nodeInfo[i].frame));
		printf("[ [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		       "  [ %8.4f, %8.4f, %8.4f, %8.4f ] ]\n"
			"  %08x == flags\n",
			mat->right.x, mat->up.x, mat->at.x, mat->pos.x,
			mat->right.y, mat->up.y, mat->at.y, mat->pos.y,
			mat->right.z, mat->up.z, mat->at.z, mat->pos.z,
			0.0f, 0.0f, 0.0f, 1.0f,
			mat->flags);
	}
}

Frame**
makeFrameList1(Frame *frame, Frame **flist)
{
	*flist++ = frame;
	if(frame->child)
		flist = makeFrameList1(frame->child, flist);
	if(frame->next)
		flist = makeFrameList1(frame->next, flist);
	return flist;
}

Frame**
makeFrameList2(Frame *frame, Frame **flist)
{
	if(frame->next)
		flist = makeFrameList2(frame->next, flist);
	*flist++ = frame;
	if(frame->child)
		flist = makeFrameList2(frame->child, flist);
	return flist;
}

bool
checkFrameList(HAnimHierarchy *hier, Frame **flist)
{
	int i;
	for(i = 0; i < hier->numNodes; i++){
		int flags = 0;
		if(flist[i]->next)
			flags |= HAnimHierarchy::PUSH;
		if(flist[i]->child == nil)
			flags |= HAnimHierarchy::POP;
		if(hier->nodeInfo[i].flags != flags)
			return false;
	}
	return true;
}

void
assignIDs(HAnimHierarchy *hier)
{
	int i;
	int nextID = 2000;
	if(hier->numNodes != hier->parentFrame->count()){
		fprintf(stderr, "error: hierarchies don't match\n");
		return;
	}
	Frame **frames1 = rwNewT(Frame*, hier->numNodes, 0);
	Frame **frames2 = rwNewT(Frame*, hier->numNodes, 0);
	makeFrameList1(hier->parentFrame, frames1);
	makeFrameList2(hier->parentFrame, frames2);

	Frame **flist = nil;
	if(checkFrameList(hier, frames1))
		flist = frames1;
	else if(checkFrameList(hier, frames2))
		flist = frames2;
	else
		fprintf(stderr, "error: hierarchies don't match\n");

	// Have a matching topology. let's assume it's correct
	if(flist){
		printf("FOUND a hierarchy!\n");
		for(i = 0; i < hier->numNodes; i++){
			hier->nodeInfo[i].frame = flist[i];
		printf("%s\n", getFrameName(hier->nodeInfo[i].frame));
			hier->nodeInfo[i].id = nextID;
			HAnimData::get(flist[i])->id = nextID;
			nextID++;
		}
	}

	rwFree(frames1);
	rwFree(frames2);
}

void
dumpMatFXData(Material *m)
{
	if(MatFX::getEffects(m) != MatFX::ENVMAP)
		return;
	MatFX *matfx = MatFX::get(m);
	int i = matfx->getEffectIndex(MatFX::ENVMAP);
	MatFX::Env *env = &matfx->fx[i].env;
	printf("\tenv: %4.2f ", env->coefficient);
	if(env->tex)
		printf("%-32s ", env->tex->name);
	printf("\n");

}

void
setupMatFX_VCPS2Xbox(Material *m)
{
	if(MatFX::getEffects(m) != MatFX::ENVMAP)
		return;
	MatFX *matfx = MatFX::get(m);
	int i = matfx->getEffectIndex(MatFX::ENVMAP);
	MatFX::Env *env = &matfx->fx[i].env;

	if(env->coefficient){
		env->coefficient = 1.0f;
		m->surfaceProps.specular = 0.5f;
	}else
		m->surfaceProps.specular = 0.0f;
}

void
dumpMat(Material *m)
{
	// remove primary color
//	if(m->color.red == 255 && m->color.green == 0 && m->color.blue == 175){
//		m->color.red = 255;
//		m->color.green = 255;
//		m->color.blue = 255;
//	}
//	if(m->color.red == 60 && m->color.green == 255 && m->color.blue == 0){
//		m->color.red = 255;
//		m->color.green = 255;
//		m->color.blue = 255;
//	}

	printf("%s %3d %3d %3d %3d amb: %.3f diff: %.3f spec: %.3f %-32s \n",
		inputfilename, m->color.red, m->color.green, m->color.blue, m->color.alpha,
		m->surfaceProps.ambient, m->surfaceProps.diffuse, m->surfaceProps.specular,
		m->texture ? m->texture->name : "NULL"
		);
	dumpMatFXData(m);
}

int
setSpecMap(Material *m, const char *name)
{
	if(m->surfaceProps.specular != 0.0f){
		MatFX::setEffects(m, MatFX::ENVMAP);
		MatFX *mfx = MatFX::get(m);
		mfx->setEnvCoefficient(m->surfaceProps.specular);
		rw::Texture *tex = rw::Texture::read(name, nil);
		mfx->setEnvTexture(tex);
		return 1;
	}
	return 0;
}

void
dumpReflData(Material *m)
{
	using namespace gta;

	MatFX *matfx = MatFX::get(m);
	if(m->pipeline->pluginID != ID_PDS)
		return;
	printf("%02x ", m->pipeline->pluginData & 0xff);

	SpecMat *spec = getSpecMat(m);
	if(spec)
		printf("spec(%4.3f) ", spec->specularity);

	EnvMat *env = getEnvMat(m);
	if(env)
		printf("env(%4.3f) ", env->getShininess());

	if(matfx){
		printf("matfx(%d", matfx->type);
		Texture *tex = matfx->getEnvTexture();
		if(tex)
			printf(", %s", tex->name);
		printf(") ");
	}

	printf("\n");
}

bool
isPrimaryColor(RGBA *c)
{
	return c->red == 0x3C && c->green == 0xFF && c->blue == 0x00;
}

bool
isSecondaryColor(RGBA *c)
{
	return c->red == 0xFF && c->green == 0x00 && c->blue == 0xAF;
}

void
removeBodyTextures(Clump *clump, bool all)
{
	FORLIST(lnk, clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int32 i = 0; i < g->matList.numMaterials; i++){
			Material *m = g->matList.materials[i];
			if(m->texture == nil) continue;
			if(strstr(m->texture->name, "body") &&
			   all || (isPrimaryColor(&m->color) || isSecondaryColor(&m->color))){
				m->texture->destroy();
				m->texture = nil;
			}
		}
	}
}

void
resetSurfProps(Clump *clump)
{
	FORLIST(lnk, clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int32 i = 0; i < g->matList.numMaterials; i++){
			Material *m = g->matList.materials[i];
			//if(m->texture)
			//	printf("%24s ", m->texture->name);
			//printf("%f %f %f\n", m->surfaceProps.ambient, m->surfaceProps.diffuse, m->surfaceProps.specular);
			//m->surfaceProps.diffuse = 0.0f;
			m->surfaceProps.ambient = 1.0f;
			m->surfaceProps.diffuse = 1.0f;
			m->surfaceProps.specular = 0.0f;
		}
	}
}

void
removeUnusedMaterials(Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		Atomic::fromClump(lnk)->geometry->removeUnusedMaterials();
}

void
removeEffects(Atomic *atomic)
{
	Geometry *geo = atomic->geometry;
	for(int i = 0; i < geo->matList.numMaterials; i++){
		Material *mat = geo->matList.materials[i];
		MatFX *matfx = *PLUGINOFFSET(MatFX*, mat, matFXGlobals.materialOffset);
		if(matfx){
			matfx->type = 0;
			matfx->fx[0].type = 0;
			matfx->fx[1].type = 0;
		}
		*PLUGINOFFSET(int32*, atomic, matFXGlobals.atomicOffset) = 0;
	}
}

void
extractmultiple(StreamFile &in)
{
	ChunkHeaderInfo header;
	char filename[100];
	StreamFile out;
	for(;readChunkHeaderInfo(&in, &header);){
		if(rw::version == 0){
			rw::version = header.version;
			rw::build = header.build;
		}

		if(header.type != ID_CLUMP){
			in.close();
			return;
		}
		rw::Clump *clump = rw::Clump::streamRead(&in);
		if(clump == nil){
			printf("error: couldn't read clump\n");
			return;
		}
		printf("%s\n", gta::getNodeName(clump->getFrame()));
		strcpy(filename, gta::getNodeName(clump->getFrame()));
		strcat(filename, ".dff");

		if(!out.open(filename, "wb")){
			fprintf(stderr, "Error: couldn't open %s\n", filename);
			return;
		}
		clump->streamWrite(&out);
		out.close();
	}
}

int
vertexAlpha(RGBA *col, int n)
{
	uint8 alpha = 0xFF;
	while(n--)
		alpha &= col++->alpha;
	return alpha != 0xFF;
}

int
main(int argc, char *argv[])
{
	rw::version = 0;
//	rw::version = 0x34003;
//	rw::version = 0x33002;
//	rw::platform = rw::PLATFORM_PS2;
//	rw::platform = rw::PLATFORM_OGL;
//	rw::platform = rw::PLATFORM_XBOX;
	rw::platform = rw::PLATFORM_D3D8;
//	rw::platform = rw::PLATFORM_D3D9;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();

	rw::Texture::setCreateDummies(1);

	int uninstance = 0;
	int instance = 0;
	int dump = 0;
	int surfprops = 0;
	int removebody = 0;
	int outplatform = rw::PLATFORM_D3D8;
	int correctWinding = 0;
	int multiclump = 0;
	int setwhite = 0;
	int iiiToVcCar = 0;
	const char *specmap = "reflection01";
	int ps2vccar = 0;
	int xboxbuild = 0;
	int ps2sacar = 0;
	int ps2sabuilding = 0;
	int ps2saped = 0;
	int info = 0;
	int dumpmat = 0;
	int tristrip = 0;
	int json = 0;
	int assign = 0;
	int centerUV = 0;

	char *s, *longarg;
	//char *seconddff = NULL;
	ARGBEGIN{
	case '-':
		// hack for long options: _args is the long option
		// and must be an empty string in the end
		longarg = _args;
		_args = (char*)"";
		if(strcmp_ci(longarg, "ps2vccar") == 0) ps2vccar++;
		else if(strcmp_ci(longarg, "iii2vccar") == 0){
			iiiToVcCar++;
			specmap = EARGF(usage());
		}else if(strcmp_ci(longarg, "info") == 0) info++;
		else if(strcmp_ci(longarg, "xboxbuilding") == 0) xboxbuild++;
		else if(strcmp_ci(longarg, "ps2sacar") == 0) ps2sacar++;
		else if(strcmp_ci(longarg, "ps2sabuilding") == 0) ps2sabuilding++;
		else if(strcmp_ci(longarg, "ps2saped") == 0) ps2saped++;
		else if(strcmp_ci(longarg, "centerUV") == 0) centerUV++;
		else usage();
		break;
	case 'u':
		uninstance++;
		break;
	case 'i':
		instance++;
		break;
	//case 'c':
	//	seconddff = EARGF(usage());
	//	break;
	case 'v':
		sscanf(EARGF(usage()), "%x", &rw::version);
		break;
	case 'f':
		rw::streamAppendFrames = 1;
		break;
	case 'd':
		dump++;
		break;
	case 't':
		dumpmat++;
		break;
	case 's':
		tristrip++;
		break;
	case 'r':
		surfprops++;
		break;
	case 'b':
		removebody++;
		break;
	case 'w':
		correctWinding++;
		break;
	case 'm':
		multiclump++;
		break;
	case 'W':
		setwhite++;
		break;
	case 'j':
		json++;
		break;
	case 'a':
		assign++;
		break;
	case 'o':
		s = EARGF(usage());
		for(int i = 0; platforms[i].str; i++){
			if(strcmp(platforms[i].str, s) == 0){
				outplatform = platforms[i].val;
				goto found;
			}
		}
		printf("unknown platform %s\n", s);
		outplatform = PLATFORM_D3D8;
	found:
		break;
	default:
		usage();
	}ARGEND;

	if(uninstance && instance){
		fprintf(stderr, "cannot both instance and uninstance, choose one!\n");
		return 1;
	}

	if(argc < 1)
		usage();

	Clump *c;
	//uint32 len;
	//uint8 *data = getFileContents(argv[0], &len);
	//assert(data != NULL);
	//StreamMemory in;
	//in.open(data, len);

	inputfilename = argv[0];
	StreamFile in;
	if(!in.open(argv[0], "rb")){
		fprintf(stderr, "Error: couldn't open %s\n", argv[0]);
		return 1;
	}

	if(multiclump){
		extractmultiple(in);
		return 0;
	}

	currentUVAnimDictionary = NULL;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(&in, &header);
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(&in);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(&in, &header);
	}
	if(header.type != ID_CLUMP){
		in.close();
		return 0;
	}
	debugFile = argv[0];
	c = Clump::streamRead(&in);
	in.close();
	if(c == NULL){
		fprintf(stderr, "Error: couldn't read clump\n");
		return 1;
	}

	if(rw::version == 0){
		rw::version = header.version;
		rw::build = header.build;
	}

	if(surfprops)
		resetSurfProps(c);
	if(removebody)
		removeBodyTextures(c, removebody == 1);

	//Clump *colclump = NULL;
	//if(seconddff){
	//	in.open(seconddff, "rb");
	//	readChunkHeaderInfo(&in, &header);
	//	if(header.type != ID_CLUMP){
	//		in.close();
	//		return 0;
	//	}
	//	debugFile = seconddff;
	//	colclump = Clump::streamRead(&in);
	//	assert(colclump != NULL);
	//	in.close();
	//
	//	c->copyPlugins(colclump);
	//}

//	printf("%s\n", argv[arg]);

/*
	for(int32 i = 0; i < c->numAtomics; i++){
		Atomic *a = c->atomicList[i];
		Pipeline *ap = a->pipeline;
		Geometry *g = a->geometry;
		for(int32 j = 0; j < g->numMaterials; j++){
			Pipeline *mp = g->materialList[j]->pipeline;
			if(ap && mp)
				printf("%s %x %x\n", argv[arg], ap->pluginData, mp->pluginData);
		}
	}
*/
	//FORLIST(lnk, c->lights){
	//	Light *l = Light::fromClump(lnk);
	//	printf("%p %p\n", l, lnk);
	//	printf("%d %f %f %f\n", l->getType(), l->color.red, l->color.green, l->color.blue);
	//}

	if(assign){
		HAnimHierarchy *hier = HAnimHierarchy::find(c->getFrame());
		if(hier)
			assignIDs(hier);
	}

	if(dump){
		HAnimHierarchy *hier = HAnimHierarchy::find(c->getFrame());
		if(hier)
			hier->attach();
		dumpFrameHier(c->getFrame(), dump > 1);

		if(dump > 2)
			FORLIST(lnk, c->atomics){
				Atomic *a = Atomic::fromClump(lnk);
				Geometry *g = a->geometry;
				if(Skin::get(g))
					dumpBoneMatrices(Skin::get(g), hier);
			}
	}

	//if(currentUVAnimDictionary){
	//	FORLIST(lnk, currentUVAnimDictionary->animations){
	//		UVAnimDictEntry *de = UVAnimDictEntry::fromDict(lnk);
	//		Animation *anim = de->anim;
	//		dumpUVAnim(anim);
	//	}
	//}

	//FORLIST(lnk, c->atomics){
	//	Atomic *atomic = Atomic::fromClump(lnk);
	//	Frame *f = atomic->getFrame();
	//	if(strstr(gta::getNodeName(f), "wheel"))
	//		removeEffects(atomic);
	//}

	if(setwhite)
		FORLIST(lnk, c->atomics){
			Geometry *g = Atomic::fromClump(lnk)->geometry;
			for(int i = 0; i < g->matList.numMaterials; i++){
				Material *m = g->matList.materials[i];
				m->color.red = 0xFF;
				m->color.green = 0xFF;
				m->color.blue = 0xFF;
			}
		}

	if(dumpmat)
		FORLIST(lnk, c->atomics){
			Geometry *g = Atomic::fromClump(lnk)->geometry;
			for(int i = 0; i < g->matList.numMaterials; i++){
				Material *m = g->matList.materials[i];
		//		dumpMat(m);
				dumpReflData(m);
			}
		}

	if(iiiToVcCar)
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			Geometry *g = a->geometry;
			int hasenv = 0;
			for(int i = 0; i < g->matList.numMaterials; i++){
				Material *m = g->matList.materials[i];
				hasenv |= setSpecMap(m, specmap);
			}
			if(hasenv)
				MatFX::enableEffects(a);
		}

	if(ps2vccar)
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
//			gta::setPipelineID(a, 0);
//			hAnimDoStream = 0;
			Geometry *g = a->geometry;
			for(int i = 0; i < g->matList.numMaterials; i++){
				Material *m = g->matList.materials[i];
				setupMatFX_VCPS2Xbox(m);
			}
		}

	// Make sure we have all pipes attached for uninstance
	FORLIST(lnk, c->atomics)
		gta::attachCustomPipelines(Atomic::fromClump(lnk));;
	int32 platform = findPlatform(c);
	if(platform){
		rw::platform = platform;
		switchPipes(c, rw::platform);
	}

	if(uninstance)
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			a->uninstance();
			if(outplatform != PLATFORM_PS2)
				ps2::unconvertADC(a->geometry);
		}

	rw::platform = outplatform;
	switchPipes(c, rw::platform);

	if(xboxbuild)
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			convertSAXboxBuilding(a);
		}
	if(ps2sacar){
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			assignPS2SAcarPipes(a);
		}
	}
	if(ps2sabuilding){
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			assignPS2SAbuildingPipes(a);
		}
	}
	if(ps2saped){
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			assignPS2SAPedPipe(a);
		}
	}

	removeUnusedMaterials(c);

	if(correctWinding)
		FORLIST(lnk, c->atomics){
			Geometry *g = Atomic::fromClump(lnk)->geometry;
			g->correctTristripWinding();
			g->generateTriangles();
		}

	if(centerUV)
		FORLIST(lnk, c->atomics){
			Geometry *g = Atomic::fromClump(lnk)->geometry;
			centerTexCoords(g);
		}

	if(tristrip)
		FORLIST(lnk, c->atomics){
			Geometry *g = Atomic::fromClump(lnk)->geometry;
			g->flags |= Geometry::TRISTRIP;
			g->buildMeshes();
		}

	if(instance)
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			if(outplatform != PLATFORM_PS2)
				ps2::convertADC(a->geometry);
			a->instance();
		}

	if(json){
		const char *file;
		if(argc > 1)
			file = argv[1];
		else
			file = "out.json";
		dumpjson(c, file);
		return 0;
	}

	if(info){
		int output = 0;
#define PRINT(fmt) do { if(!output) printf("%s: ", inputfilename); else putchar(' '); printf(fmt); output=1; }while(0)
		if(currentUVAnimDictionary)
			PRINT("uvanim");

		int hasvertalpa = 0;
		int haswetroad = 0;
		int hasskin = 0;
		int isnative = 0;
		int extracols = 0;
		int extranorms = 0;
		FORLIST(lnk, c->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			Geometry *g = a->geometry;

			if(Skin::get(g))
				hasskin = 1;

			if(g->flags & Geometry::NATIVE)
				isnative = 1;

			RGBA *extracol = gta::getExtraVertColors(a);
			V3d *extranorm = gta::getExtraNormals(g);
			if(extracol) extracols = 1;
			if(extranorm) extranorms = 1;
			if(g->flags & Geometry::PRELIT && !isnative){
				assert(g->colors);
				if(vertexAlpha(g->colors, g->numVertices)){
					if(extracol) haswetroad = 1;
					else hasvertalpa = 1;
				}
				if(extracol && vertexAlpha(extracol, g->numVertices))
					hasvertalpa = 1;
			}
		}
		if(isnative) PRINT("instanced");
		if(hasskin) PRINT("skin");
		if(hasvertalpa) PRINT("vertalpha");
		if(extracols) PRINT("extracolors");
		if(extranorms) PRINT("extranormals");
		if(haswetroad) PRINT("wetroad");

		if(output)
			putchar('\n');
	}

	StreamFile out;
	const char *file;
	if(argc > 1)
		file = argv[1];
	else
		file = "out.dff";
	if(!out.open(file, "wb")){
		fprintf(stderr, "Error: couldn't open %s\n", file);
		return 1;
	}
	if(currentUVAnimDictionary)
		currentUVAnimDictionary->streamWrite(&out);
	c->streamWrite(&out);
	out.close();

//	data = rwNewT(uint8, 1024*1024, 0);
//	rw::StreamMemory out;
//	out.open(data, 0, 1024*1024);
//	if(currentUVAnimDictionary)
//		currentUVAnimDictionary->streamWrite(&out);
//	c->streamWrite(&out);
//
//	FILE *cf;
//	if(argc > 1)
//		cf = fopen(argv[1], "wb");
//	else
//		cf = fopen("out.dff", "wb");
//	assert(cf != NULL);
//	fwrite(data, out.getLength(), 1, cf);
//	fclose(cf);
//	out.close();
//	rwFree(data);

	c->destroy();
	if(currentUVAnimDictionary)
		currentUVAnimDictionary->destroy();

	return 0;
}
