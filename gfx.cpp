#include "editor.h"
#include "config.h"

#define STB_TRUETYPE_IMPLEMENTATION

#include "vendor/glad.h"
#include "vendor/stb_truetype.h"

#define ATLAS_SIZE   1024
#define FIRST_CHAR   32
#define NUM_CHARS    96
#define MAX_VERTICES 4098

#define MAX_U16 65535

funcdef u32 gfx__compile_shader(int type, string src);
funcdef Rect gfx__rect_intersect(Rect a, Rect b);
funcdef void gfx__rebuild_font_atlas(f32 pixel_height);

enum {
	Uniform_Resolution,
	Uniform_Textures,
	Uniform_Count,
};

struct Vertex {
	struct { s16 x, y; } pos;
	struct { u16 u, v; } uv;   // normalized [0, MAX_U16] maps to [0.0, 1.0]
	u32 color;
	u16 tex_id;
	struct { s8 x, y; } circle; 
	struct { u16 x0, y0, x1, y1; } clip;
};

static struct  {
	Arena *persist;

	OS_TimeStamp last_frame_time;
	f32 delta_time;

	list<Vertex> vertices;
	list<u16>    indices;

	u32 vao, vbo, ebo;
	u32 program;

	s32 uniforms[Uniform_Count];
	u32 textures[Texture_Count];

	stbtt_fontinfo  font;
	stbtt_bakedchar baked_chars[NUM_CHARS];
	f32 ascent, line_height;
	f32 font_height;

	Render_Clip *clip_stack;
} gfx_ctx;

funcdef void
gfx_init(OS_Handle window, Arena *persist)
{
	if (!gladLoadGLLoader((GLADloadproc)os_get_gl_proc_address())) {
		fprintf(stderr, "failed to load opengl");
		return;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	gfx_ctx.persist = persist;

	////////////// opengl renderer setup //////////////

	auto vertex_buf  = alloc_slice(persist, Vertex, MAX_VERTICES);
	gfx_ctx.vertices = list_make(vertex_buf);

	auto index_buf   = alloc_slice(persist, u16, MAX_VERTICES);
	gfx_ctx.indices  = list_make(index_buf);

	glGenVertexArrays(1, &gfx_ctx.vao);
	glGenBuffers(1, &gfx_ctx.vbo);
	glGenBuffers(1, &gfx_ctx.ebo);

	glBindVertexArray(gfx_ctx.vao);

	glBindBuffer(GL_ARRAY_BUFFER, gfx_ctx.vbo);
	glBufferData(GL_ARRAY_BUFFER, MAX_VERTICES * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gfx_ctx.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_VERTICES * sizeof(u16), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, uv));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, color));

	glEnableVertexAttribArray(3);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void *)offsetof(Vertex, tex_id));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 2, GL_BYTE, GL_TRUE, sizeof(Vertex), (void *)offsetof(Vertex, circle));

	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 4, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, clip));


	{
		string vs = S(
			"#version 330 core\n"
			"layout (location = 0) in vec2  a_pos;"
			"layout (location = 1) in vec2  a_uv;"
			"layout (location = 2) in vec4  a_color;"
			"layout (location = 3) in uint  a_texid;"
			"layout (location = 4) in vec2  a_circ;"
			"layout (location = 5) in vec4  a_clip;"
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
			"   gl_Position = vec4(ndc, 0.0, 1.0);"
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
			"   float aa = fwidth(dist);"
			"   float mask = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, dist);"
			"   Frag_Color = v_color * pixel;"
			"   Frag_Color.a *= mask;"
			"}"
		);

		u32 vs_id = gfx__compile_shader(GL_VERTEX_SHADER,   vs);
		u32 fs_id = gfx__compile_shader(GL_FRAGMENT_SHADER, fs);

		assert(vs_id && fs_id);

		gfx_ctx.program = glCreateProgram();
		glAttachShader(gfx_ctx.program, vs_id);
		glAttachShader(gfx_ctx.program, fs_id);
		glLinkProgram(gfx_ctx.program);

		glDeleteShader(vs_id);
		glDeleteShader(fs_id);

		glUseProgram(gfx_ctx.program);

		gfx_ctx.uniforms[Uniform_Resolution] = glGetUniformLocation(gfx_ctx.program, "u_resolution");
		gfx_ctx.uniforms[Uniform_Textures]   = glGetUniformLocation(gfx_ctx.program, "u_textures");

		int samplers[Texture_Count] = {0, 1};
		glUniform1iv(gfx_ctx.uniforms[Uniform_Textures], Texture_Count, samplers);
	}

	{
		u32 white = 0xFFFFFFFF;
		glGenTextures(1, &gfx_ctx.textures[Texture_White]);
		glBindTexture(GL_TEXTURE_2D, gfx_ctx.textures[Texture_White]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	{
		glGenTextures(1, &gfx_ctx.textures[Texture_Font]);
		glBindTexture(GL_TEXTURE_2D, gfx_ctx.textures[Texture_Font]);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);

		gfx_set_font_height(FONT_HEIGHT);
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gfx_ctx.textures[Texture_White]);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gfx_ctx.textures[Texture_Font]);

	gfx_ctx.last_frame_time = os_time_now();

	ivec2 size = os_window_size(window);
	gfx_set_viewport(size.x, size.y);
}


funcdef void
gfx_set_font_height(f32 height)
{
	if (height <= 0)
		return;

	gfx_ctx.font_height = height;

	gfx__rebuild_font_atlas(height);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(
		GL_TEXTURE_2D,
		gfx_ctx.textures[Texture_Font]
	);
}

funcdef f32
gfx_get_font_height()
{
	return gfx_ctx.font_height;
}

funcdef void
gfx_deinit()
{
	glDeleteVertexArrays(1, &gfx_ctx.vao);
	glDeleteBuffers(1, &gfx_ctx.vbo);
	glDeleteBuffers(1, &gfx_ctx.ebo);
	glDeleteProgram(gfx_ctx.program);
	glDeleteTextures(Texture_Count, gfx_ctx.textures);
}

funcdef void
gfx_begin()
{
	OS_TimeStamp curr = os_time_now();
	gfx_ctx.delta_time = (f32) os_time_diff(
		gfx_ctx.last_frame_time, curr
	).seconds;
	gfx_ctx.last_frame_time = curr;

	glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

funcdef void
gfx_submit()
{
	glBindVertexArray(gfx_ctx.vao);

	glBindBuffer(GL_ARRAY_BUFFER, gfx_ctx.vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(gfx_ctx.vertices.len * sizeof(Vertex)), gfx_ctx.vertices.raw);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gfx_ctx.ebo);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(gfx_ctx.indices.len * sizeof(u16)), gfx_ctx.indices.raw);

	glUseProgram(gfx_ctx.program);
	glDrawElements(GL_TRIANGLES, (GLsizei)gfx_ctx.indices.len, GL_UNSIGNED_SHORT, 0);

	clear(&gfx_ctx.vertices);
	clear(&gfx_ctx.indices);
}


funcdef void
gfx_set_viewport(s32 width, s32 height)
{
	glViewport(0, 0, (int)width, (int)height);
	glUseProgram(gfx_ctx.program);
	glUniform2f(gfx_ctx.uniforms[Uniform_Resolution], (float)width, (float)height);
}

funcdef f32 
delta_time()
{
	return gfx_ctx.delta_time;
}

funcdef f32 
gfx_line_height()
{
	return gfx_ctx.line_height;
}

inline bool
rects_overlap(f32 ax, f32 ay, f32 aw, f32 ah, f32 bx, f32 by, f32 bw, f32 bh)
{
	return !(ax + aw <= bx || ay + ah <= by || ax >= bx + bw || ay >= by + bh);
}


funcdef void
draw_quad(vec2 pos, vec2 size, u32 color, u8 texture, vec2 uv0, vec2 uv1, ivec2 circ0, ivec2 circ1)
{
	if (gfx_ctx.vertices.len + 4 > gfx_ctx.vertices.capacity || gfx_ctx.indices.len + 6 > gfx_ctx.indices.capacity)
	{
		gfx_submit();
	}

	s8 cr0_x = (s8) circ0.x;
	s8 cr0_y = (s8) circ0.y;
	s8 cr1_x = (s8) circ1.x;
	s8 cr1_y = (s8) circ1.y;

	f32 x = pos.x,  y = pos.y;
	f32 w = size.x, h = size.y;

	Rect clip = gfx_ctx.clip_stack->rect;

	if (!rects_overlap(x, y, w, h, clip.from.x, clip.from.y, clip.size.x, clip.size.y))
		return;

	u16 i = (u16)(gfx_ctx.vertices.len);

	u16 u0 = (u16)(uv0.x * MAX_U16);
	u16 v0 = (u16)(uv0.y * MAX_U16);
	u16 u1 = (u16)(uv1.x * MAX_U16);
	u16 v1 = (u16)(uv1.y * MAX_U16);

	u16 c0 = (u16)(clip.from.x);
	u16 c1 = (u16)(clip.from.y);
	u16 c2 = (u16)(clip.from.x + clip.size.x);
	u16 c3 = (u16)(clip.from.y + clip.size.y);

	append(&gfx_ctx.vertices, Vertex { { (s16)x,       (s16)y }, { u0, v0 }, color, texture, {cr0_x, cr0_y}, { c0, c1, c2, c3 } });
	append(&gfx_ctx.vertices, Vertex { { (s16)(x + w), (s16)y }, { u1, v0 }, color, texture, {cr1_x, cr0_y}, { c0, c1, c2, c3 } });
	append(&gfx_ctx.vertices, Vertex { { (s16)(x + w), (s16)(y + h) }, { u1, v1 }, color, texture, {cr1_x, cr1_y}, { c0, c1, c2, c3 } });
	append(&gfx_ctx.vertices, Vertex { { (s16)x,       (s16)(y + h) }, { u0, v1 }, color, texture, {cr0_x, cr1_y}, { c0, c1, c2, c3 } });

	append(&gfx_ctx.indices, (u16)(i + 0));
	append(&gfx_ctx.indices, (u16)(i + 1));
	append(&gfx_ctx.indices, (u16)(i + 2));
	append(&gfx_ctx.indices, (u16)(i + 2));
	append(&gfx_ctx.indices, (u16)(i + 3));
	append(&gfx_ctx.indices, (u16)(i + 0));
}


funcdef vec2
draw_text(string s, vec2 start_pos, u32 color)
{
	f32 x = start_pos.x;
	f32 y = start_pos.y + gfx_ctx.ascent;

	f32 max_x = x;
	f32 min_y = y;
	f32 max_y = y;

	u64 column = 0;

	int width = 0;
	for (int i = 0; i < (int)s.len; i += width) {
		rune c = utf8_decode(s.range(i, s.len), &width);

		if (c == '\n') {
			x  = start_pos.x;
			y += gfx_line_height();

			column = 0;

			if (y > max_y) max_y = y;
			continue;
		}

		if (c == '\r') {
			continue;
		}

		if (c == '\t') {
			u64 spaces = TAB_WIDTH - (column % TAB_WIDTH);

			for (u64 t = 0; t < spaces; ++t) {
				stbtt_aligned_quad q;
				stbtt_GetBakedQuad(
					gfx_ctx.baked_chars,
					ATLAS_SIZE,
					ATLAS_SIZE,
					' ' - FIRST_CHAR,
					&x,
					&y,
					&q,
					1
				);
			}

			column += spaces;

			if (x > max_x) max_x = x;
			continue;
		}

		bool invalid = false;
		if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) {
			c = '?';
			invalid = true;
		}

		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(
			gfx_ctx.baked_chars,
			ATLAS_SIZE,
			ATLAS_SIZE,
			(int)(c - FIRST_CHAR),
			&x,
			&y,
			&q,
			1
		);

		if (c != ' ') {
			vec2 pos  = { q.x0, q.y0 };
			vec2 dims = { q.x1 - q.x0, q.y1 - q.y0 };
			vec2 uv0  = { q.s0, q.t0 };
			vec2 uv1  = { q.s1, q.t1 };

			draw_quad(
				pos,
				dims,
				invalid ? g_config.theme.error : color,
				Texture_Font,
				uv0,
				uv1,
				{0,0},
				{0,0}
			);

			if (q.y0 < min_y) min_y = q.y0;
			if (q.y1 > max_y) max_y = q.y1;
		}

		column += 1;

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
	if (radius <= 0.5f)
	{
		draw_quad(pos, size, color);
		return;
	}

	radius = Min(radius, Min(size.x, size.y) * 0.5f);

	draw_quad({pos.x, pos.y}, {radius, radius}, color, Texture_White, {}, {}, {127, 127}, {0, 0});
	draw_quad({pos.x + size.x - radius, pos.y}, {radius, radius}, color, Texture_White, {}, {}, {0, 127}, {127, 0});
	draw_quad({pos.x + size.x - radius, pos.y + size.y - radius}, {radius, radius}, color, Texture_White, {}, {}, {0, 0}, {127, 127});
	draw_quad({pos.x, pos.y + size.y - radius}, {radius, radius}, color, Texture_White, {}, {}, {127, 0}, {0, 127});

	draw_quad({pos.x + radius, pos.y}, {size.x - radius * 2, size.y}, color);
	draw_quad({pos.x, pos.y + radius}, {radius, size.y - radius * 2}, color);
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
gfx_char_width(rune c)
{
    if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) c = NUM_CHARS + FIRST_CHAR - 1;
    f32 x = 0, y = 0;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(gfx_ctx.baked_chars, ATLAS_SIZE, ATLAS_SIZE, (int)(c - FIRST_CHAR), &x, &y, &q, 1);
    return x;
}

funcdef vec2
gfx_measure_text(string s)
{
	f32 x = 0;
	f32 max_x = 0;

	f32 lines = 1;

	u64 column = 0;

	int width = 0;

	for (int i = 0; i < (int)s.len; i += width) {
		rune c = utf8_decode(
			s.range(i, s.len),
			&width
		);

		if (c == '\n') {
			if (x > max_x) {
				max_x = x;
			}

			x = 0;
			column = 0;

			lines += 1;
			continue;
		}

		if (c == '\r') {
			continue;
		}

		if (c == '\t') {
			u64 spaces = TAB_WIDTH - (column % TAB_WIDTH);

			x += gfx_char_width(' ') * spaces;
			column += spaces;

			if (x > max_x) {
				max_x = x;
			}

			continue;
		}

		x += gfx_char_width(c);
		column += 1;

		if (x > max_x) {
			max_x = x;
		}
	}

	return {
		max_x,
		lines * gfx_line_height()
	};
}

funcdef void
gfx_push_clip(Rect rect, Arena *frame_alloc)
{
	if (gfx_ctx.clip_stack) {
		rect = gfx__rect_intersect(rect, gfx_ctx.clip_stack->rect);
	}

	Render_Clip *clip = alloc_struct(frame_alloc, Render_Clip);
	clip->rect = rect;
	clip->next = gfx_ctx.clip_stack;

	gfx_ctx.clip_stack = clip;
}

funcdef Render_Clip
gfx_pop_clip()
{
	Render_Clip clip = {};
	clip.rect = gfx_ctx.clip_stack->rect;

	gfx_ctx.clip_stack = gfx_ctx.clip_stack->next;
	return clip;
}


funcdef u32
gfx__compile_shader(int type, string src)
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

funcdef Rect
gfx__rect_intersect(Rect a, Rect b)
{
	Rect result = {};

	f32 x0 = Max(a.from.x, b.from.x);
	f32 y0 = Max(a.from.y, b.from.y);

	f32 x1 = Min(a.from.x + a.size.x, b.from.x + b.size.x);
	f32 y1 = Min(a.from.y + a.size.y, b.from.y + b.size.y);

	result.from.x = x0;
	result.from.y = y0;

	result.size.x = Max(0.0f, x1 - x0);
	result.size.y = Max(0.0f, y1 - y0);

	return result;
}


funcdef void
gfx__rebuild_font_atlas(f32 pixel_height)
{
	Temp t = temp_begin(gfx_ctx.persist);
	defer(temp_end(t));

	bytes bitmap     = alloc_slice(t.arena, u8, ATLAS_SIZE * ATLAS_SIZE);
	bytes ttf_buffer = FALLBACK_FONT;

	assert(ttf_buffer.raw);

	stbtt_InitFont(&gfx_ctx.font, ttf_buffer.raw, 0);

	int result = stbtt_BakeFontBitmap(
		ttf_buffer.raw,
		0,
		pixel_height,
		bitmap.raw,
		ATLAS_SIZE,
		ATLAS_SIZE,
		FIRST_CHAR,
		NUM_CHARS,
		gfx_ctx.baked_chars
	);

	assert(result > 0);

	int ascent, descent, line_gap;
	stbtt_GetFontVMetrics(
		&gfx_ctx.font,
		&ascent,
		&descent,
		&line_gap
	);

	f32 scale = stbtt_ScaleForPixelHeight(
		&gfx_ctx.font,
		pixel_height
	);

	gfx_ctx.ascent      = ascent * scale;
	gfx_ctx.line_height = (ascent - descent + line_gap) * scale;

	glBindTexture(
		GL_TEXTURE_2D,
		gfx_ctx.textures[Texture_Font]
	);

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_R8,
		ATLAS_SIZE,
		ATLAS_SIZE,
		0,
		GL_RED,
		GL_UNSIGNED_BYTE,
		bitmap.raw
	);
}
