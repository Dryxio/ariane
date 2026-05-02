const char *neoWorldIII_frag_src =
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"uniform vec4 u_lm;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN vec2 v_tex1;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 t0 = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));\n"
"	vec4 t1 = texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));\n"
"	vec4 color = t0*v_color*(1 + u_lm*(2*t1-1));\n"
"	color.a = v_color.a*t0.a*u_lm.a;\n"
"	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"
;
