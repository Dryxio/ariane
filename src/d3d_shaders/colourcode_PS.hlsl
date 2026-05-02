struct VS_out {
	float4 Position		: POSITION;
	float3 TexCoord0	: TEXCOORD0;
	float4 Color		: COLOR0;
};

sampler2D tex0 : register(s0);

float4 globalColor : register(c1);

float4 main(VS_out input) : COLOR
{
	float4 color = globalColor;
	color.a *= tex2D(tex0, input.TexCoord0.xy).a;
	return color;
}
