uniform sampler2D tex : register(s0);
uniform float3 params : register(c12);

#define limit (params.x)
#define intensity (params.y)
#define numPasses (params.z)

float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float4 fb = tex2D(tex, texcoord);
	float3 hilights = saturate(fb.rgb - float3(limit/2.0, limit/2.0, limit/2.0));
	fb.rgb += hilights*intensity*4*numPasses;
	fb.a = 1.0f;
	return fb;
}
