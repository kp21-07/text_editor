#include "editor.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#define RGFW_NO_X11_CURSOR

#include "vendor/glad.h"
#include "vendor/glad.c"
#include "vendor/rgfw.h"
#include "vendor/stb_truetype.h"

#include "config.h"

#define ATLAS_SIZE   512
#define FIRST_CHAR   32
#define NUM_CHARS    96
#define MAX_VERTICES 4098

#define MAX_U16 65535

enum {
	Uniform_Resolution,
	Uniform_Textures,
	Uniform_Count,
};

enum : u16 {
	Texture_White,
	Texture_Font,
	Texture_Count
};

enum : u8 {
	Draw_Layer_Base,
	Draw_Layer_Popup,
};

struct Vertex {
	struct { s16 x, y; } pos;
	struct { u16 u, v; } uv;   // normalized [0, MAX_U16] maps to [0.0, 1.0]
	u32 color;
	u8 tex_id;
	u8 draw_layer;
	struct { s8 x, y; } circle; 
	struct { u16 x0, y0, x1, y1; } clip;
};

const u64 vertex_size = sizeof(Vertex);

global struct {
	RGFW_window *win;

	List<Vertex> vertices;
	List<u16>    indices;
	u8 draw_layer;

	u32 vao, vbo, ebo;
	u32 program;

	s32 uniforms[Uniform_Count];
	u32 textures[Texture_Count];

	stbtt_fontinfo  font;
	stbtt_bakedchar baked_chars[NUM_CHARS];
	f32 ascent, line_height;

	Render_Clip *clip_stack;
} gfx;

funcdef u32
graphics__compile_shader(int type, string src)
{
	u32 id = glCreateShader(type);

	const char *cstr = (char *)src.raw;
	int len = (int)src.len;

	glShaderSource(id, 1, &cstr, &len);
	glCompileShader(id);

	int success;
	glGetShaderiv(id, GL_COMPILE_STATUS, &success);

	if (!success) {
		char log[1024];
		glGetShaderInfoLog(id, sizeof(log), 0, log);
		printf("Shader compile error:\n%s\n", log);
		return 0;
	}

	return id;
}

funcdef void
graphics_init(const char *title, int width, int height, Arena *persist)
{
	RGFW_glHints *hints = RGFW_getGlobalHints_OpenGL();
	hints->major = 3;
	hints->minor = 3;
	RGFW_setGlobalHints_OpenGL(hints);

	RGFW_window *win = alloc_struct(persist, RGFW_window);
	RGFW_createWindowPtr(
		title, 0, 0, width, height,
		RGFW_windowCenter | RGFW_windowAllowDND | RGFW_windowOpenGL,
		win
	);

	RGFW_window_makeCurrentContext_OpenGL(win);
	RGFW_window_swapInterval_OpenGL(win, 0);
	RGFW_window_setMinSize(win, 200, 200);

	if (!gladLoadGLLoader((GLADloadproc)RGFW_getProcAddress_OpenGL)) {
		fprintf(stderr, "failed to load opengl");
		return;
	}

	gfx.draw_layer = Draw_Layer_Base;

	////////////// opengl renderer setup //////////////

	auto vertex_buf  = alloc_slice(persist, Vertex, MAX_VERTICES);
	gfx.vertices     = list_from_buffer(vertex_buf);

	auto index_buf   = alloc_slice(persist, u16, MAX_VERTICES);
	gfx.indices      = list_from_buffer(index_buf);

	glGenVertexArrays(1, &gfx.vao);
	glGenBuffers(1, &gfx.vbo);
	glGenBuffers(1, &gfx.ebo);

	glBindVertexArray(gfx.vao);

	glBindBuffer(GL_ARRAY_BUFFER, gfx.vbo);
	glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(Vertex), NULL, GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gfx.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_VERTICES * sizeof(u16), NULL, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, uv));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, color));

	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), (void *)offsetof(Vertex, tex_id));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, draw_layer));

	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 2, GL_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, circle));

	glEnableVertexAttribArray(6);
	glVertexAttribPointer(6, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, clip));

	{
		string vs = S(
			"#version 330 core\n"
			"layout (location = 0) in vec2  a_pos;"
			"layout (location = 1) in vec2  a_uv;"
			"layout (location = 2) in vec4  a_color;"
			"layout (location = 3) in uint  a_texid;"
			"layout (location = 4) in float a_layer;"
			"layout (location = 5) in vec2  a_circ;"
			"layout (location = 6) in vec4  a_clip;"
			"out vec4       v_color;"
			"out vec2       v_uv;"
			"out vec2       v_pos;"
			"out vec2       v_circ;"
			"out vec4       v_clip;"
			"flat out uint  v_texid;"
			"uniform vec2   u_resolution;"
			"void main() {"
			"   v_color  = a_color;"
			"   v_uv     = a_uv;"
			"   v_texid  = a_texid;"
			"	v_pos    = a_pos;"
			"	v_circ   = a_circ;"
			"	v_clip   = a_clip;"
			"   vec2 ndc = (a_pos / u_resolution) * 2.0 - 1.0;"
			"   ndc.y    = -ndc.y;"
			"   gl_Position = vec4(ndc, -a_layer, 1.0);"
			"}"
		);

		string fs = S(
			"#version 330 core\n"
			"in vec4       v_color;"
			"in vec2       v_uv;"
			"in vec2       v_pos;"
			"in vec2       v_circ;"
			"in vec4       v_clip;"
			"flat in uint  v_texid;"
			"out vec4      Frag_Color;"
			"uniform sampler2D u_textures[2];"
			"void main() {"
			"   if (v_pos.x < v_clip.x || v_pos.y < v_clip.y ||"
			"       v_pos.x > v_clip.z || v_pos.y > v_clip.w)"
			"       discard;"
			"   vec4 pixel;"
			"   if (v_texid == 0u) { pixel = texture(u_textures[0], v_uv); }"
			"   else               { pixel = texture(u_textures[1], v_uv); }"
			"   float dist = length(v_circ);"
			"   float aa = fwidth(dist) * 0.5;"
			"   float mask = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, dist);"
			"   Frag_Color = v_color * pixel;"
			"   Frag_Color.a *= mask;"
			"}"
		);

		u32 vs_id = graphics__compile_shader(GL_VERTEX_SHADER,   vs);
		u32 fs_id = graphics__compile_shader(GL_FRAGMENT_SHADER, fs);

		assert(vs_id && fs_id);

		gfx.program = glCreateProgram();
		glAttachShader(gfx.program, vs_id);
		glAttachShader(gfx.program, fs_id);
		glLinkProgram(gfx.program);

		glDeleteShader(vs_id);
		glDeleteShader(fs_id);

		glUseProgram(gfx.program);

		gfx.uniforms[Uniform_Resolution] = glGetUniformLocation(gfx.program, "u_resolution");
		gfx.uniforms[Uniform_Textures]   = glGetUniformLocation(gfx.program, "u_textures");

		int samplers[Texture_Count] = {0, 1};
		glUniform1iv(gfx.uniforms[Uniform_Textures], Texture_Count, samplers);
	}

	{
		u32 white = 0xFFFFFFFF;
		glGenTextures(1, &gfx.textures[Texture_White]);
		glBindTexture(GL_TEXTURE_2D, gfx.textures[Texture_White]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	{
		u64 arena_mark = persist->used;

		const int   atlas_w      = ATLAS_SIZE;
		const int   atlas_h      = ATLAS_SIZE;
		const float pixel_height = FONT_HEIGHT;

		bytes bitmap    = alloc_slice(persist, u8, atlas_w * atlas_h);
		bytes ttf_buffer = platform_load_entire_file(S("./jetbrains_mono.ttf"), persist);

		stbtt_InitFont(&gfx.font, ttf_buffer.raw, 0);

		int result = stbtt_BakeFontBitmap(
			ttf_buffer.raw, 0, pixel_height,
			bitmap.raw, atlas_w, atlas_h,
			FIRST_CHAR, NUM_CHARS,
			gfx.baked_chars
		);

		if (result <= 0)
			printf("Font bake failed\n");

		int ascent, descent, line_gap;
		stbtt_GetFontVMetrics(&gfx.font, &ascent, &descent, &line_gap);
		float scale     = stbtt_ScaleForPixelHeight(&gfx.font, pixel_height);
		gfx.ascent      = ascent * scale;
		gfx.line_height = (ascent - descent + line_gap) * scale;

		glGenTextures(1, &gfx.textures[Texture_Font]);
		glBindTexture(GL_TEXTURE_2D, gfx.textures[Texture_Font]);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas_w, atlas_h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.raw);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);

		arena_free(persist, arena_mark);
	}

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gfx.textures[Texture_White]);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gfx.textures[Texture_Font]);

	gfx.win = win;
}

funcdef bool
graphics_update(u32 color, Frame_Input *input)
{
	*input = {};	

	if (RGFW_window_shouldClose(gfx.win)) return false;

	f32 a = ((color >> 24) & 0xFF) / 255.0f;
	f32 b = ((color >> 16) & 0xFF) / 255.0f;
	f32 g = ((color >>  8) & 0xFF) / 255.0f;
	f32 r = ((color >>  0) & 0xFF) / 255.0f;

	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindVertexArray(gfx.vao);

	glBindBuffer(GL_ARRAY_BUFFER, gfx.vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(gfx.vertices.len * sizeof(Vertex)), gfx.vertices.raw);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gfx.ebo);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(gfx.indices.len * sizeof(u16)), gfx.indices.raw);

	glUseProgram(gfx.program);
	glUniform2f(gfx.uniforms[Uniform_Resolution], (float)gfx.win->w, (float)gfx.win->h);

	glDrawElements(GL_TRIANGLES, (GLsizei)gfx.indices.len, GL_UNSIGNED_SHORT, 0);

	clear(&gfx.vertices);
	clear(&gfx.indices);

	RGFW_window_swapBuffers_OpenGL(gfx.win);

	// RGFW_waitForEvent(-1);
	RGFW_event event = {0};
	while (RGFW_window_checkEvent(gfx.win, &event)) {
		switch (event.type) {
			case RGFW_windowResized: {
				glViewport(0, 0, (int)gfx.win->w, (int)gfx.win->h);
				break;
			}

			case RGFW_keyPressed:  {
				RGFW_keyEvent k = event.key;
				switch(k.value){
					case RGFW_keyBackSpace: input->key_flags |= key_Backspace; break;
					case RGFW_keyDelete: input->key_flags |= key_Delete; break;
					case RGFW_keyEscape: input->key_flags |= key_Escape; break;
					case RGFW_keyReturn: input->character = '\n'; break;
					case RGFW_keyTab: input->character = '\t'; break;
				}
				break;
			}

			case RGFW_keyChar: {
				RGFW_keyCharEvent k = event.keyChar;
				if (!unicode_visual_rune(k.value)) break;
				input->character = k.value;
				break;
			}
		}
	}

	return true;
}


force_inline bool
rects_overlap(f32 ax, f32 ay, f32 aw, f32 ah, f32 bx, f32 by, f32 bw, f32 bh)
{
	return !(ax + aw <= bx || ay + ah <= by || ax >= bx + bw || ay >= by + bh);
}


funcdef u8
draw_push_layer(u8 new_layer)
{
	u8 old = gfx.draw_layer;
	gfx.draw_layer = new_layer;
	return old;
}

funcdef void
draw_quad(vec2 pos, vec2 size, u32 color, u8 texture, vec2 uv0, vec2 uv1, ivec2 circ0, ivec2 circ1)
{
	s8 cr0_x = (s8) circ0.x;
	s8 cr0_y = (s8) circ0.y;
	s8 cr1_x = (s8) circ1.x;
	s8 cr1_y = (s8) circ1.y;

	f32 x = pos.x,  y = pos.y;
	f32 w = size.x, h = size.y;

	Rect clip = gfx.clip_stack->rect;

	if (!rects_overlap(x, y, w, h, clip.from.x, clip.from.y, clip.size.x, clip.size.y))
		return;

	u16 i = (u16)(gfx.vertices.len);

	u16 u0 = (u16)(uv0.x * MAX_U16);
	u16 v0 = (u16)(uv0.y * MAX_U16);
	u16 u1 = (u16)(uv1.x * MAX_U16);
	u16 v1 = (u16)(uv1.y * MAX_U16);

	u16 c0 = (u16)(clip.from.x);
	u16 c1 = (u16)(clip.from.y);
	u16 c2 = (u16)(clip.from.x + clip.size.x);
	u16 c3 = (u16)(clip.from.y + clip.size.y);

	append(&gfx.vertices, Vertex { { (s16)x,       (s16)y }, { u0, v0 }, color, texture, gfx.draw_layer, {cr0_x, cr0_y}, { c0, c1, c2, c3 } });
	append(&gfx.vertices, Vertex { { (s16)(x + w), (s16)y }, { u1, v0 }, color, texture, gfx.draw_layer, {cr1_x, cr0_y}, { c0, c1, c2, c3 } });
	append(&gfx.vertices, Vertex { { (s16)(x + w), (s16)(y + h) }, { u1, v1 }, color, texture, gfx.draw_layer, {cr1_x, cr1_y}, { c0, c1, c2, c3 } });
	append(&gfx.vertices, Vertex { { (s16)x,       (s16)(y + h) }, { u0, v1 }, color, texture, gfx.draw_layer, {cr0_x, cr1_y}, { c0, c1, c2, c3 } });

	append(&gfx.indices, (u16)(i + 0));
	append(&gfx.indices, (u16)(i + 1));
	append(&gfx.indices, (u16)(i + 2));
	append(&gfx.indices, (u16)(i + 2));
	append(&gfx.indices, (u16)(i + 3));
	append(&gfx.indices, (u16)(i + 0));
}

funcdef vec2
draw_text(string s, vec2 start_pos, u32 color)
{
	f32 x = start_pos.x;
	f32 y = start_pos.y + gfx.ascent;
    
	f32 max_x = x;
	f32 min_y = y;
	f32 max_y = y;
    
	int width = 0;
	for (int i = 0; i < (int)s.len; i += width) {
		rune c = utf8_decode(slice(s, i, s.len), &width);
        
		if (c == '\n') {
			x  = start_pos.x;
			y += gfx.line_height;
            
			if (y > max_y) max_y = y;
			continue;
		}
        
		if (c == '\r') continue;
        
		if (c == '\t') {
			for (int t = 0; t < 4; ++t) {
				stbtt_aligned_quad q;
				stbtt_GetBakedQuad(gfx.baked_chars, 512, 512, ' ' - FIRST_CHAR, &x, &y, &q, 1);
			}
			if (x > max_x) max_x = x;
			continue;
		}
        
		bool invalid = false;
		if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) {
			c = NUM_CHARS + FIRST_CHAR - 1;
			invalid = true;
		}
        
		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(gfx.baked_chars, 512, 512, (int)(c - FIRST_CHAR), &x, &y, &q, 1);
        
		if (c != ' ') {
			vec2 pos  = { q.x0, q.y0 };
			vec2 dims = { q.x1 - q.x0, q.y1 - q.y0 };
			vec2 uv0  = { q.s0, q.t0 };
			vec2 uv1  = { q.s1, q.t1 };
            
			draw_quad(pos, dims, invalid ? 0xFF0000FF : color, Texture_Font, uv0, uv1);
            
			if (q.y0 < min_y) min_y = q.y0;
			if (q.y1 > max_y) max_y = q.y1;
		}
        
		if (x > max_x) max_x = x;
	}
    
	vec2 result;
	result.x = max_x - start_pos.x;
	result.y = max_y - min_y;
    
	return result;
}

funcdef void
draw_quad_rounded(vec2 pos, vec2 size, f32 radius, u32 color)
{
	draw_quad({pos.x, pos.y}, {radius, radius}, color, Texture_White, {}, {}, {127, 127}, {0, 0});
	draw_quad({pos.x + size.x - radius, pos.y}, {radius, radius}, color, Texture_White, {}, {}, {0, 127}, {127, 0});
	draw_quad({pos.x + size.x - radius, pos.y + size.y - radius}, {radius, radius}, color, Texture_White, {}, {}, {0, 0}, {127, 127});
	draw_quad({pos.x, pos.y + size.y - radius}, {radius, radius}, color, Texture_White, {}, {}, {127, 0}, {0, 127});
	draw_quad({pos.x + radius, pos.y}, {size.x - radius * 2, size.y}, color);
	draw_quad({pos.x, pos.y + radius}, {size.x - radius * 2, size.y - radius * 2}, color);
	draw_quad({pos.x + size.x - radius, pos.y + radius}, {radius, size.y - radius * 2}, color);
}

funcdef void
draw_capsule(vec2 pos, vec2 size, u32 color)
{
	f32 radius = Min(size.x, size.y) * 0.5f;

	if (size.x >= size.y) {
		draw_quad({pos.x, pos.y}, {radius, size.y}, color, 0, {}, {}, {-128, -128}, {0, 127});
		draw_quad({pos.x + radius, pos.y}, {size.x - size.y, size.y}, color);
		draw_quad({pos.x + size.x - radius, pos.y}, {radius, size.y}, color, 0, {}, {}, {0, 127}, {127, -128});
	}
	else {
		draw_quad({pos.x, pos.y}, {size.x, radius}, color, 0, {}, {}, {-128, -128}, {127, 0});
		draw_quad({pos.x, pos.y + radius}, {size.x, size.y - size.x}, color);
		draw_quad({pos.x, pos.y + size.y - radius}, {size.x, radius}, color, 0, {}, {}, {127, 0}, {-128, -128});
	}
}

funcdef f32
graphics_char_width(rune c)
{
    if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) c = NUM_CHARS + FIRST_CHAR - 1;
    f32 x = 0, y = 0;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(gfx.baked_chars, ATLAS_SIZE, ATLAS_SIZE, (int)(c - FIRST_CHAR), &x, &y, &q, 1);
    return x;
}

funcdef vec2
graphics_measure_text(string s)
{
    f32 x     = 0;
    f32 max_x = 0;
    f32 lines = 1;
    int width = 0;
    for (int i = 0; i < (int)s.len; i += width) {
        rune c = utf8_decode(slice(s, i, s.len), &width);
        if (c == '\n') {
            if (x > max_x) max_x = x;
            x = 0;
            lines += 1;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\t') {
            x += graphics_char_width(' ') * 4;
            if (x > max_x) max_x = x;
            continue;
        }
        x += graphics_char_width(c);
        if (x > max_x) max_x = x;
    }
    return { max_x, lines * gfx.line_height };
}


funcdef void
graphics_push_clip(Rect rect, Arena *frame_alloc)
{
	Render_Clip *clip = alloc_struct(frame_alloc, Render_Clip);
	clip->rect = rect;
	clip->next = gfx.clip_stack;
	gfx.clip_stack = clip;
}

funcdef Render_Clip
graphics_pop_clip()
{
	Render_Clip clip = {};
	clip.rect = gfx.clip_stack->rect;

	gfx.clip_stack = gfx.clip_stack->next;

	return clip;
}
