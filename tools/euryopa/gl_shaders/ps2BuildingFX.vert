VSIN(ATTRIB_POS)	vec3 in_pos;

VSOUT vec4 v_color;
VSOUT vec2 v_tex0;

uniform mat4  u_envmat;
uniform vec4  u_envXform;
uniform float u_shininess;

void
main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * u_view * Vertex;

	v_color = vec4(192.0, 192.0, 192.0, 0.0)/128.0*u_shininess;
	v_tex0 = (u_envmat * vec4(in_normal, 0.0f)).xy - u_envXform.xy;
	v_tex0 *= u_envXform.zw;
}
