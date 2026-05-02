const char *ps2BuildingFX_vert_src =
"VSIN(ATTRIB_POS)	vec3 in_pos;\n"

"VSOUT vec4 v_color;\n"
"VSOUT vec2 v_tex0;\n"

"uniform mat4  u_envmat;\n"
"uniform vec4  u_envXform;\n"
"uniform float u_shininess;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 Vertex = u_world * vec4(in_pos, 1.0);\n"
"	gl_Position = u_proj * u_view * Vertex;\n"

"	v_color = vec4(192.0, 192.0, 192.0, 0.0)/128.0*u_shininess;\n"
"	v_tex0 = (u_envmat * vec4(in_normal, 0.0f)).xy - u_envXform.xy;\n"
"	v_tex0 *= u_envXform.zw;\n"
"}\n"
;
