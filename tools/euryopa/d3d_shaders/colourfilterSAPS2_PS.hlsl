uniform sampler2D tex : register(s0);
uniform float4 col1 : register(c10);
uniform float4 col2 : register(c11);


float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float4 fb = tex2D(tex, texcoord);
	fb = fb*col1*2 + fb*col2*2*col2.a*2;
	fb.a = 1.0f;
	return fb;
}
