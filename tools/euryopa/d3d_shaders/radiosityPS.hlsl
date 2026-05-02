uniform sampler2D tex : register(s0);
uniform float3 params : register(c12);

#define limit (params.x)
#define intensity (params.y)
#define renderpasses (params.z)

float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float4 fb = tex2D(tex, texcoord);
	fb += saturate(2*fb - limit)*intensity*renderpasses;
	fb.a = 1.0f;
	return fb;
}
