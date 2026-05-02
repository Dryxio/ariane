sampler2D tex : register(s0);
float4 fogColor : register(c0);
float4 colorscale : register(c1);

struct PS_INPUT
{
	float3 texcoord0	: TEXCOORD0;
	float4 color		: COLOR0;
};

float4
main(PS_INPUT IN) : COLOR
{
	float4 color = tex2D(tex, IN.texcoord0.xy)*IN.color*colorscale.x;
	color.rgb = lerp(fogColor.rgb, color.rgb, IN.texcoord0.z);
	return color;
}
