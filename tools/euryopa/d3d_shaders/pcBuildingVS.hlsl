float4x4	combined	: register(c0);
float3		ambient		: register(c4);
float4		fogData		: register(c14);
float4		matCol		: register(c19);
float3		surfProps	: register(c20);

float4		shaderParams	: register(c29);
float4		dayparam	: register(c30);
float4		nightparam	: register(c31);
float4x4	texmat		: register(c32);

#define colorScale (shaderParams.x)
#define surfAmb (surfProps.x)

#define fogStart (fogData.x)
#define fogEnd (fogData.y)
#define fogRange (fogData.z)
#define fogDisable (fogData.w)

struct VS_INPUT
{
	float4 Position		: POSITION;
	float2 TexCoord		: TEXCOORD0;
	float4 NightColor	: COLOR0;
	float4 DayColor		: COLOR1;
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
	OUT.Texcoord0.xy = mul(texmat, float4(IN.TexCoord, 0.0, 1.0)).xy;

	OUT.Color = IN.DayColor*dayparam + IN.NightColor*nightparam;
	OUT.Color.rgb += ambient*surfAmb;
	OUT.Color *= matCol / colorScale;

	OUT.Texcoord0.z = clamp((OUT.Position.w - fogEnd)*fogRange, fogDisable, 1.0);

	return OUT;
}
