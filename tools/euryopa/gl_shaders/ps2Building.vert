VSIN(ATTRIB_POS)	vec3 in_pos;

VSOUT vec4 v_color;
VSOUT vec2 v_tex0;
VSOUT float v_fog;

uniform vec4 u_dayparam;
uniform vec4 u_nightparam;
uniform float u_colorscale;
uniform mat4  u_texmat;

#define in_nightcolor in_color
#define in_daycolor in_weights

void
main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * u_view * Vertex;
	vec3 Normal = mat3(u_world) * in_normal;

	v_color = in_nightcolor*u_nightparam + in_daycolor*u_dayparam;
	v_color *= u_matColor / u_colorscale;
	v_color.rgb += u_ambLight.rgb*surfAmbient;

	v_tex0 = (u_texmat * vec4(in_tex0, 0.0f, 1.0f)).xy;

	v_fog = DoFog(gl_Position.w);
}
