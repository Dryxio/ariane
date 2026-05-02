float4x4	combined	: register(c0);
float4		matCol		: register(c12);
float4		surfProps	: register(c13);
float4		fogData		: register(c14);

float3		emissive	: register(c16);
float3		ambient		: register(c17);

#define surfAmb (surfProps.x)
#define surfEmiss (surfProps.w)

#define fogStart (fogData.x)
#define fogEnd (fogData.y)
#define fogRange (fogData.z)
#define fogDisable (fogData.w)

struct VS_INPUT
{
	float4 Position		: POSITION;
	float2 TexCoord		: TEXCOORD0;
	float4 Color		: COLOR0;
};

struct VS_OUTPUT {
	float4 Position		: POSITION;
	float3 Texcoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

VS_OUTPUT main(in VS_INPUT IN)
{
	VS_OUTPUT OUT;

	OUT.Position = mul(combined, IN.Position);
	OUT.Texcoord0.xy = IN.TexCoord;

	OUT.Color = saturate(IN.Color*float4(ambient, 1.0f) + float4(emissive, 0.0f)*surfEmiss);
	OUT.Color.w *= matCol.w;

	OUT.Texcoord0.z = clamp((OUT.Position.w - fogEnd)*fogRange, fogDisable, 1.0);

	return OUT;
}
