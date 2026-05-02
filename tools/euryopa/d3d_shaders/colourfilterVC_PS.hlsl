uniform sampler2D tex : register(s0);
uniform float4 blurcol : register(c10);


float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float a = 30/255.0f;
	float4 doublec = saturate(blurcol*2);
	float4 dst = tex2D(tex, texcoord.xy);
	float4 prev = dst;
	for(int i = 0; i < 5; i++){
		float4 tmp = dst*(1-a) + prev*doublec*a;
		tmp += prev*blurcol;
		tmp += prev*blurcol;
		prev = saturate(tmp);
	}
	prev.a = 1.0f;
	return prev;
}
