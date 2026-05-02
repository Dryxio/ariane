sampler2D tex : register(s0);

struct VS_OUTPUT {
	float4 position		: POSITION;
	float3 texcoord1	: TEXCOORD0;
	float4 envcolor		: COLOR0;
};

float4
main(VS_OUTPUT IN) : COLOR
{
	float4 col = tex2D(tex, IN.texcoord1.xy) * IN.envcolor;
	col.a = 1.0;
	return col;
}
