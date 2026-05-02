const char *ps2Building_vert_src =
"VSIN(ATTRIB_POS)	vec3 in_pos;\n"

"VSOUT vec4 v_color;\n"
"VSOUT vec2 v_tex0;\n"
"VSOUT float v_fog;\n"

"uniform vec4 u_dayparam;\n"
"uniform vec4 u_nightparam;\n"
"uniform float u_colorscale;\n"
"uniform mat4  u_texmat;\n"

"#define in_nightcolor in_color\n"
"#define in_daycolor in_weights\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 Vertex = u_world * vec4(in_pos, 1.0);\n"
"	gl_Position = u_proj * u_view * Vertex;\n"
"	vec3 Normal = mat3(u_world) * in_normal;\n"

"	v_color = in_nightcolor*u_nightparam + in_daycolor*u_dayparam;\n"
"	v_color *= u_matColor / u_colorscale;\n"
"	v_color.rgb += u_ambLight.rgb*surfAmbient;\n"

"	v_tex0 = (u_texmat * vec4(in_tex0, 0.0f, 1.0f)).xy;\n"

"	v_fog = DoFog(gl_Position.w);\n"
"}\n"
;
