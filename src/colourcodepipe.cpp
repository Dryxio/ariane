#define WITH_D3D
#include <rw.h>
#include "rwgta.h"
#include <assert.h>

namespace gta {

bool renderColourCoded;
rw::RGBA colourCode;

#ifdef RW_D3D9

using namespace rw;
using namespace d3d;
using namespace d3d9;

void *colourcode_PS;

enum {
	PSLOC_globalColor = 1
};


void
colourCodeRenderCB(Atomic *atomic, d3d9::InstanceDataHeader *header)
{
	Geometry *geo = atomic->geometry;

	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	uint32 flags = geo->flags;
	geo->flags &= ~Geometry::LIGHT;
	lightingCB_Shader(atomic);
	geo->flags = flags;

	uploadMatrices(atomic->getFrame()->getLTM());

	setVertexShader(default_amb_VS);
	setPixelShader(colourcode_PS);

	RGBAf c;
	convColor(&c, &colourCode);
	d3ddevice->SetPixelShaderConstantF(PSLOC_globalColor, (float*)&c, 1);

	InstanceData *inst = header->inst;
	uint32 blend;
	for(uint32 i = 0; i < header->numMeshes; i++){
		d3d::setTexture(0, inst->material->texture);
		SetRenderState(VERTEXALPHA, inst->vertexAlpha || colourCode.alpha != 255);

		d3d::getRenderState(D3DRS_ALPHABLENDENABLE, &blend);
		if(renderColourCoded)
			d3d::setRenderState(D3DRS_ALPHABLENDENABLE, 0);

		d3d9::drawInst(header, inst);

		d3d::setRenderState(D3DRS_ALPHABLENDENABLE, blend);
		inst++;
	}
}

rw::ObjPipeline*
makeColourCodePipeline(void)
{
	{
#include "d3d_shaders/colourcode_PS.inc"
		colourcode_PS = createPixelShader(colourcode_PS_cso);
		assert(colourcode_PS);
	}

	d3d9::ObjPipeline *pipe = d3d9::ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = colourCodeRenderCB;
	return pipe;
}

int32
GetColourCode(int x, int y)
{
	int32 res = 0;
	IDirect3DSurface9 *backbuffer = nil;
	d3ddevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);

	D3DLOCKED_RECT d3dlr;
	D3DSURFACE_DESC desc;
	IDirect3DSurface9 *surf = nil;
	backbuffer->GetDesc(&desc);
	d3ddevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
		D3DPOOL_SYSTEMMEM, &surf, nil);
	d3ddevice->GetRenderTargetData(backbuffer, surf);

	surf->LockRect(&d3dlr, nil, D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_READONLY);
	// TODO: check format and dimensions properly
	if(desc.Format == D3DFMT_A8R8G8B8){
		uint8 *col = (uint8*)d3dlr.pBits + d3dlr.Pitch*y + x*4;
		res = col[0]<<16 | col[1]<<8 | col[2];
	}
	surf->UnlockRect();

	surf->Release();
	backbuffer->Release();
	return res;
}

int
GetColourCodesInRect(int rx, int ry, int w, int h, int32 *out, int maxOut)
{
	int count = 0;
	if(w <= 0 || h <= 0 || maxOut <= 0) return 0;

	IDirect3DSurface9 *backbuffer = nil;
	d3ddevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);

	D3DLOCKED_RECT d3dlr;
	D3DSURFACE_DESC desc;
	IDirect3DSurface9 *surf = nil;
	backbuffer->GetDesc(&desc);
	d3ddevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
		D3DPOOL_SYSTEMMEM, &surf, nil);
	d3ddevice->GetRenderTargetData(backbuffer, surf);

	surf->LockRect(&d3dlr, nil, D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_READONLY);
	if(desc.Format == D3DFMT_A8R8G8B8){
		int xEnd = rx + w < (int)desc.Width  ? rx + w : (int)desc.Width;
		int yEnd = ry + h < (int)desc.Height ? ry + h : (int)desc.Height;
		for(int row = ry; row < yEnd; row++){
			uint8 *scanline = (uint8*)d3dlr.pBits + d3dlr.Pitch*row;
			for(int col = rx; col < xEnd; col++){
				uint8 *px = scanline + col*4;
				int32 code = px[0]<<16 | px[1]<<8 | px[2];
				if(code == 0) continue;
				bool found = false;
				for(int j = 0; j < count; j++)
					if(out[j] == code){ found = true; break; }
				if(!found){
					out[count++] = code;
					if(count >= maxOut) goto done;
				}
			}
		}
	}
done:
	surf->UnlockRect();
	surf->Release();
	backbuffer->Release();
	return count;
}

#endif

#ifdef RW_GL3

using namespace rw;
using namespace gl3;

Shader *colourCodeShader;

void
colourCodeRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	Material *m;
	RGBAf col;
	Geometry *geo = atomic->geometry;

	setWorldMatrix(atomic->getFrame()->getLTM());
	uint32 flags = geo->flags;
	geo->flags &= ~Geometry::LIGHT;
	lightingCB(atomic);
	geo->flags = flags;

	setupVertexInput(header);

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	colourCodeShader->use();

	convColor(&col, &colourCode);
	setUniform(u_matColor, &col);

	while(n--){
		m = inst->material;

		setTexture(0, m->texture);
		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || colourCode.alpha != 0xFF);

		int blend = getAlphaBlend();
		if(renderColourCoded)
			setAlphaBlend(false);

		drawInst(header, inst);

		setAlphaBlend(blend);
		inst++;
	}
	teardownVertexInput(header);
}

rw::ObjPipeline*
makeColourCodePipeline(void)
{
	{
#include "gl_shaders/colcode_vert.inc"
#include "gl_shaders/colcode_frag.inc"
	const char *vs[] = { shaderDecl, header_vert_src, colcode_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, colcode_frag_src, nil };
	colourCodeShader = Shader::create(vs, fs);
	assert(colourCodeShader);
	}

	gl3::ObjPipeline *pipe = gl3::ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = colourCodeRenderCB;
	return pipe;
}

int32
GetColourCode(int x, int y)
{
	rw::RGBA col;
	int viewport[4];
	// TODO: check format and dimensions properly
	glGetIntegerv(GL_VIEWPORT, viewport); 
	glReadPixels(x, viewport[3]-y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &col);
	return col.blue<<16 | col.green<<8 | col.red;
}

int
GetColourCodesInRect(int rx, int ry, int w, int h, int32 *out, int maxOut)
{
	int count = 0;
	if(w <= 0 || h <= 0 || maxOut <= 0) return 0;

	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	int glx = rx;
	int gly = viewport[3] - ry - h;
	rw::RGBA *pixels = (rw::RGBA*)rwMalloc(w * h * sizeof(rw::RGBA), 0);
	if(pixels == nil) return 0;
	glReadPixels(glx, gly, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	// glReadPixels returns bottom-up rows; iterate all pixels
	for(int i = 0; i < w*h; i++){
		int32 code = pixels[i].blue<<16 | pixels[i].green<<8 | pixels[i].red;
		if(code == 0) continue;
		bool found = false;
		for(int j = 0; j < count; j++)
			if(out[j] == code){ found = true; break; }
		if(!found){
			out[count++] = code;
			if(count >= maxOut) break;
		}
	}
	rwFree(pixels);
	return count;
}
#endif

}
