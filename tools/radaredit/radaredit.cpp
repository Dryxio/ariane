#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <ctype.h>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

char *argv0;

using namespace std;
using namespace rw;

void
copyImage(rw::Image *dst, int dstx, int dsty, rw::Image *src, int srcx, int srcy, int w, int h)
{
	int i;
	if(dst->depth != src->depth)
		return;
	uint8 *dstp = &dst->pixels[dsty*dst->stride + dstx*dst->bpp];
	uint8 *srcp = &src->pixels[srcy*src->stride + srcx*src->bpp];

	for(i = 0; i < h; i++){
		memcpy(dstp, srcp, w*src->bpp);
		dstp += dst->stride;
		srcp += src->stride;
	}
}

int
getAlpha(rw::Image *mask, uint8 *px)
{
	assert(mask->depth != 16);

	if(mask->depth <= 8)
		px = &mask->palette[*px * 4];
	// 32 bit
	return ((int)px[0] + (int)px[1] + (int)px[2])/3;
}

bool
applyMask(rw::Image *img, rw::Image *mask)
{
	if(img->width != mask->width ||
	   img->height != mask->height ||
	   img->depth != 32)
		return false;

	int i, j;
	uint8 *imgp = img->pixels;
	uint8 *maskp = mask->pixels;
	for(i = 0; i < img->height; i++){
		for(j = 0; j < img->width; j++)
			imgp[j*4 + 3] = getAlpha(mask, &maskp[j*mask->depth/8]);
		imgp += img->stride;
		maskp += mask->stride;
	}
	return true;
}

static uint8 palette[256*4];
static int ncolors;

int
findColor(uint8 *px)
{
	int i;
	for(i = 0; i < ncolors; i++)
		if(palette[i*4 + 0] == px[0] &&
		   palette[i*4 + 1] == px[1] &&
		   palette[i*4 + 2] == px[2] &&
		   palette[i*4 + 3] == px[3])
			return i;
	return -1;
}

rw::Image*
makeIndexed(rw::Image *img)
{
	int i, j;
	int n;
	uint8 *srcrow, *dstrow;

	ncolors = 0;

	srcrow = img->pixels;
	for(i = 0; i < img->height; i++){
		for(j = 0; j < img->width; j++){
			n = findColor(&srcrow[j*img->bpp]);
			if(n < 0){
				if(ncolors >= 256)
					return nil;
				palette[ncolors*4 + 0] = srcrow[j*img->bpp + 0];
				palette[ncolors*4 + 1] = srcrow[j*img->bpp + 1];
				palette[ncolors*4 + 2] = srcrow[j*img->bpp + 2];
				palette[ncolors*4 + 3] = srcrow[j*img->bpp + 3];
				ncolors++;
			}
		}
		srcrow += img->stride;
	}

	rw::Image *pal = rw::Image::create(img->width, img->height, 8);
	pal->palette = palette;
	pal->allocate();

	srcrow = img->pixels;
	dstrow = pal->pixels;
	for(i = 0; i < img->height; i++){
		for(j = 0; j < img->width; j++){
			n = findColor(&srcrow[j*img->bpp]);
			assert(n >= 0);
			dstrow[j] = n;
		}
		srcrow += img->stride;
		dstrow += pal->stride;
	}

	return pal;
}

void
usage(void)
{
	fprintf(stderr, "usage: %s -x nhoriz nvert directory texturename [maskname]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	rw::version = 0x33002;
	rw::platform = rw::PLATFORM_D3D8;
	int extract = 0;
	int nhoriz = 0;
	int nvert = 0;
	char *path;
	char *texname;
	char *maskname;
	rw::Image *fullradar;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();
	rw::Texture::setLoadTextures(false);
//	rw::d3d::isP8supported = 0;

	ARGBEGIN{
	case 'x':
		extract++;
		break;
	default:
		usage();
	}ARGEND;


	if(argc < 4)
		usage();

	nhoriz = atoi(argv[0]);
	nvert = atoi(argv[1]);
	path = argv[2];
	texname = argv[3];
	maskname = argc >= 4 ? argv[4] : nil;

	ChunkHeaderInfo header;
	rw::TexDictionary *txd;
	rw::Texture *tex;
	rw::Image *img;

	int i, j;
	static char txdname[1024];

	if(extract){
		fullradar = rw::Image::create(128*nhoriz, 128*nvert, 32);
		fullradar->allocate();

		for(i = 0; i < nvert; i++)
			for(j = 0; j < nhoriz; j++){
				snprintf(txdname, 1024, "%s/radar%02d.txd", path, i*nhoriz + j);
				rw::StreamFile in;
				if(in.open(txdname, "rb") == NULL){
					fprintf(stderr, "couldn't open file %s\n", txdname);
					return 1;
				}

				readChunkHeaderInfo(&in, &header);
				assert(header.type == ID_TEXDICTIONARY);
				txd = rw::TexDictionary::streamRead(&in);
				assert(txd);

				FORLIST(lnk, txd->textures){
					tex = rw::Texture::fromDict(lnk);
					break;
				}
				assert(tex);
				printf("%s %s\n", tex->name, tex->mask);

				assert(tex->raster->width == 128);
				assert(tex->raster->height == 128);

				img = tex->raster->toImage();
				img->unpalettize();

				copyImage(fullradar, j*128, i*128, img, 0, 0, 128, 128);

				img->destroy();

				in.close();
			}

		if(maskname){
			Image *mask = fullradar->extractMask();
			writeTGA(mask, maskname);
			fullradar->removeMask();
		}
		writeTGA(fullradar, texname);
	}else{
		fullradar = readTGA(texname);
		if(maskname){
			Image *mask = readTGA(maskname);
			if(!applyMask(fullradar, mask))
				fprintf(stderr, "warning: cannot apply mask to texture. Make sure dimensions are the same and texture is 32 bit\n");
		}

		img = rw::Image::create(128, 128, 32);
		img->allocate();
		tex = rw::Texture::create(nil);
		txd = rw::TexDictionary::create();
		txd->add(tex);

		for(i = 0; i < nvert; i++)
			for(j = 0; j < nhoriz; j++){
				copyImage(img, 0, 0, fullradar, j*128, i*128, 128, 128);
				snprintf(txdname, 1024, "%s/radar%02d.txd", path, i*nhoriz + j);
puts(txdname);
				snprintf(tex->name, 32, "radar%02d", i*nhoriz + j);
				snprintf(tex->mask, 32, "radarM%02d", i*nhoriz + j);

				rw::Image *pal = makeIndexed(img);
				if(pal){
					tex->raster = rw::Raster::createFromImage(pal);
					pal->destroy();
				}else
					tex->raster = rw::Raster::createFromImage(img);

				rw::StreamFile out;
				if(out.open(txdname, "wb") == NULL){
					fprintf(stderr, "couldn't open file %s\n", txdname);
					return 1;
				}
				txd->streamWrite(&out);
				out.close();

//				writeTGA(img, txdname);

				tex->raster->destroy();
				tex->raster = nil;
			}

		writeTGA(fullradar, "foo.tga");
	}

	return 0;
}
