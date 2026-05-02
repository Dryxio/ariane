uniform sampler2D tex0;
uniform sampler2D tex1;
uniform vec4 u_lm;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN vec2 v_tex1;
FSIN float v_fog;

void
main(void)
{
	vec4 t0 = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	vec4 t1 = texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));
	vec4 color = t0*v_color*(1 + u_lm*(2*t1-1));
	color.a = v_color.a*t0.a*u_lm.a;
	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);
	DoAlphaTest(color.a);
	FRAGCOLOR(color);
}
