#include "editor.h"
#include "config.h"

#define STB_TRUETYPE_IMPLEMENTATION

#include "vendor/glad.h"
#include "vendor/stb_truetype.h"

const u64 MAX_QUADS  = 4098;
const u64 ATLAS_SIZE = 1024;
const u64 FIRST_CHAR = 32;
const u64 NUM_CHARS  = 96;

funcdef void gfx__submit();
funcdef void gfx__apply_clip(Quad rect, s32 win_h);
funcdef Quad gfx__rect_intersect(Quad a, Quad b);
funcdef u32  gfx__compile_shader(int type, string src);
funcdef Quad gfx__rect_intersect(Quad a, Quad b);
funcdef void gfx__rebuild_font_atlas(f32 pixel_height);

enum {
	Uniform_Resolution,
	Uniform_Textures,
	Uniform_Count,
};

struct Clip_Node {
	Quad rect;
	Clip_Node *next;
};

struct Quad_Instance {
	vec4 dest;   // { position , size }
	vec4 color;  // { r, g, b, a }
	vec4 src;    // { uv0.x, uv0.y, uv1.x, uv1.y }
	vec4 config; // { radius, tex_id, pad, pad }
};

static struct  {
	Arena *persist;
	Arena *frame_arena;

	ivec2 resolution;
	OS_TimeStamp last_frame_time;
	f32 delta_time;

	list<Quad_Instance> quads;

	// quad
	GLuint quad_vao, quad_vbo, quad_ibo;
	GLuint quad_program;

	s32 uniforms[Uniform_Count];
	u32 textures[Texture_Count];

	stbtt_fontinfo  font;
	stbtt_bakedchar baked_chars[NUM_CHARS];
	f32 ascent, line_height;
	f32 font_height;

	Clip_Node *clip_stack;
} gfx_ctx;


funcdef void
gfx_init(OS_Handle window, Arena *persist, Arena *frame)
{
	if (!gladLoadGLLoader((GLADloadproc)os_get_gl_proc_address())) {
		fprintf(stderr, "failed to load opengl");
		return;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	gfx_ctx.persist = persist;
	gfx_ctx.frame_arena = frame;

	gfx_ctx.quads = list_make(alloc_slice(persist, Quad_Instance, MAX_QUADS));

	////////////// opengl renderer setup //////////////
	
	f32 quad_v[] = {
		0.0f, 0.0f,
		1.0f, 0.0f,
		1.0f, 1.0f,

		0.0f, 0.0f,
		1.0f, 1.0f,
		0.0f, 1.0f,
	};

	GLuint vao, vbo, ibo;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		sizeof(quad_v),
		quad_v,
		GL_STATIC_DRAW
	);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ARRAY_BUFFER, ibo);

	glBufferData(GL_ARRAY_BUFFER, MAX_QUADS * sizeof(Quad_Instance), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Quad_Instance), (void*)offsetof(Quad_Instance,dest));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Quad_Instance), (void*)offsetof(Quad_Instance,color));

	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Quad_Instance), (void*)offsetof(Quad_Instance,src));

	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Quad_Instance), (void*)offsetof(Quad_Instance,config));

	for (u64 i=1; i<=4; ++i) {
		glVertexAttribDivisor(i, 1);
	}

	gfx_ctx.quad_vao = vao;
	gfx_ctx.quad_vbo = vbo;
	gfx_ctx.quad_ibo = ibo;

	{
		string vs = S(
			"#version 330 core\n"

			"layout (location = 0) in vec2 a_pos;"

			"layout (location = 1) in vec4 a_dest;"
			"layout (location = 2) in vec4 a_color;"
			"layout (location = 3) in vec4 a_src;"
			"layout (location = 4) in vec4 a_config;"

			"uniform vec2 u_resolution;"

			"out vec2 v_uv;"
			"out vec2 v_local;"
			"out vec2 v_size;"
			"out vec4 v_color;"
			"out vec4 v_config;"

			"void main() {"
				"vec2 world = a_dest.xy + a_pos * a_dest.zw;"
				"vec2 ndc;"
				"ndc.x = world.x / u_resolution.x * 2.0 - 1.0;"
				"ndc.y = 1.0 - world.y / u_resolution.y * 2.0;"

				"gl_Position = vec4(ndc, 0.0, 1.0);"

				"v_uv = mix(a_src.xy, a_src.zw, a_pos);"
				"v_local = a_pos;"
				"v_size = a_dest.zw;"
				"v_color = a_color;"
				"v_config = a_config;"
			"}"
		);

		string fs = S(
			"#version 330 core\n"

			"in vec2 v_uv;"
			"in vec2 v_local;"
			"in vec2 v_size;"
			"in vec4 v_color;"
			"in vec4 v_config;"

			"out vec4 out_color;"

			"uniform sampler2D u_textures[2];"

			"float rounded_box(vec2 p, vec2 half_size, float radius) {"
				"vec2 q = abs(p) - (half_size - radius);"
				"return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;"
			"}"

			"void main() {"
				"float radius = min(v_config.x, min(v_size.x, v_size.y) * 0.5);"
				"float smoothness = max(v_config.z, 0.0);"

				"vec2 p = (v_local - 0.5) * v_size;"

				"float d = rounded_box(p, v_size * 0.5, radius);"

				"float aa = fwidth(d);"
				"float edge = aa + smoothness;"

				"float alpha = 1.0f;"
				"if (v_config.y < 0.5)"
					"alpha = smoothstep(0, -edge, d);"

				"vec4 pixel;"
				"if (v_config.y == 0.0f)"
					"pixel = texture(u_textures[0], v_uv);"
				"else "
					"pixel = texture(u_textures[1], v_uv);"

				"out_color = vec4(v_color.rgb, v_color.a * alpha) * pixel;"
			"}"
		);

		u32 vs_id = gfx__compile_shader(GL_VERTEX_SHADER,   vs);
		u32 fs_id = gfx__compile_shader(GL_FRAGMENT_SHADER, fs);

		assert(vs_id && fs_id);

		gfx_ctx.quad_program = glCreateProgram();
		glAttachShader(gfx_ctx.quad_program, vs_id);
		glAttachShader(gfx_ctx.quad_program, fs_id);
		glLinkProgram(gfx_ctx.quad_program);

		glDeleteShader(vs_id);
		glDeleteShader(fs_id);

		glUseProgram(gfx_ctx.quad_program);

		gfx_ctx.uniforms[Uniform_Resolution] = glGetUniformLocation(gfx_ctx.quad_program, "u_resolution");
		gfx_ctx.uniforms[Uniform_Textures]   = glGetUniformLocation(gfx_ctx.quad_program, "u_textures");

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
	glDeleteVertexArrays(1, &gfx_ctx.quad_vao);
	glDeleteBuffers(1, &gfx_ctx.quad_vbo);
	glDeleteBuffers(1, &gfx_ctx.quad_ibo);
	glDeleteProgram(gfx_ctx.quad_program);
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

	vec4 cc = THEME.background;

	glClearColor(cc.x, cc.y, cc.z, cc.w);
	glClear(GL_COLOR_BUFFER_BIT);

	ivec2 size = gfx_ctx.resolution;

	Clip_Node *node = alloc_struct(gfx_ctx.frame_arena, Clip_Node);
	s32 h = size.y;
	Quad rect = {
		{0, 0},
		{(f32) size.x, (f32) size.y}
	};

	node->rect = rect;
	node->next = gfx_ctx.clip_stack;
	gfx_ctx.clip_stack = node;

	gfx__submit();
	gfx__apply_clip(rect, h);
}

funcdef void
gfx_end()
{
	gfx_pop_clip();
}

funcdef void
gfx__submit()
{
	auto instances = gfx_ctx.quads;
	if (!instances.len)
		return;

	glBindBuffer(GL_ARRAY_BUFFER, gfx_ctx.quad_ibo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, instances.len * sizeof(Quad_Instance), instances.raw);

	glBindVertexArray(gfx_ctx.quad_vao);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, instances.len);

	clear(&gfx_ctx.quads);
}


funcdef void
gfx_set_viewport(s32 width, s32 height)
{
	gfx_ctx.resolution = { width, height };

	glViewport(0, 0, (int)width, (int)height);
	glUseProgram(gfx_ctx.quad_program);
	glUniform2f(gfx_ctx.uniforms[Uniform_Resolution], (float)width, (float)height);
}

funcdef void
gfx_draw_quad(vec4 dest, vec4 src, vec4 color, f32 radius, f32 blur, int tex_id)
{
	if (gfx_ctx.quads.len >= MAX_QUADS) {
		gfx__submit();
	}

	Quad_Instance quad = {};

	quad.dest = dest;
	quad.src  = src;
	quad.color = color;
	quad.config = { radius, (f32) tex_id, blur, 0 };

	append(&gfx_ctx.quads, quad);
}


funcdef vec4
gfx_draw_text(string s, vec2 position, vec4 col, f32 max_width)
{
	if (!s.len)
		return {};

    f32 x = position.x;
    f32 y = position.y + gfx_ctx.ascent;

    f32 max_x = x;
    u64 lines = 0;
    u64 column = 0;

    for (u64 i = 0; i < s.len;)
    {
        int advance = 0;
        rune c = utf8_decode(s.range(i, s.len), &advance);

        if (advance <= 0)
            break;

        i += advance;

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            if (x > max_x)
                max_x = x;

            x = position.x;
            y += gfx_ctx.line_height;

            column = 0;
            lines += 1;
            continue;
        }

        if (c == '\t')
        {
            u64 spaces = TAB_WIDTH - (column % TAB_WIDTH);

            for (u64 t = 0; t < spaces; ++t)
            {
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

            if (x > max_x)
                max_x = x;

            continue;
        }

        bool invalid = false;

        if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) {
            c = '?';
            invalid = true;
        }

        f32 test_x = x;
        f32 test_y = y;

        stbtt_aligned_quad q;

        stbtt_GetBakedQuad(
            gfx_ctx.baked_chars,
            ATLAS_SIZE,
            ATLAS_SIZE,
            (int)(c - FIRST_CHAR),
            &test_x,
            &test_y,
            &q,
            1
        );

        if (max_width > 0.0f && x > position.x && (q.x1 - position.x) > max_width) {
            x = position.x;
            y += gfx_ctx.line_height;

            lines += 1;
            column = 0;

            test_x = x;
            test_y = y;

            stbtt_GetBakedQuad(
                gfx_ctx.baked_chars,
                ATLAS_SIZE,
                ATLAS_SIZE,
                (int)(c - FIRST_CHAR),
                &test_x,
                &test_y,
                &q,
                1
            );
        }

        x = test_x;
        y = test_y;

        if (c != ' ') {
            gfx_draw_quad(
                { q.x0, q.y0, q.x1 - q.x0, q.y1 - q.y0 },
                { q.s0, q.t0, q.s1, q.t1 },
                invalid ? color(0xff0000ff) : col,
                0.0f,
                0.0f,
                1
            );
        }

        column += 1;

        if (x > max_x)
            max_x = x;
    }

	if (s[s.len - 1] != '\n')
		lines += 1;

    return {
        position.x,
        position.y,
        max_x - position.x,
        lines * gfx_ctx.line_height
    };
}

funcdef vec2
gfx_measure_text(string s, f32 max_width)
{
    if (!s.len)
        return {};

    f32 x = 0.0f;
    f32 max_x = 0.0f;

    u64 lines = 0;
    u64 column = 0;

    for (u64 i = 0; i < s.len;)
    {
        int advance = 0;
        rune c = utf8_decode(s.range(i, s.len), &advance);

        if (advance <= 0)
            break;

        i += advance;

        if (c == '\r')
            continue;

        if (c == '\n')
        {
            if (x > max_x)
                max_x = x;

            x = 0.0f;
            column = 0;
            lines += 1;
            continue;
        }

        if (c == '\t')
        {
            u64 spaces = TAB_WIDTH - (column % TAB_WIDTH);

            stbtt_bakedchar *space =
                &gfx_ctx.baked_chars[' ' - FIRST_CHAR];

            x += space->xadvance * spaces;

            column += spaces;

            if (x > max_x)
                max_x = x;

            continue;
        }

        if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS)
            c = '?';

        stbtt_bakedchar *bc =
            &gfx_ctx.baked_chars[c - FIRST_CHAR];

        f32 next_x = x + bc->xadvance;

        if (max_width > 0.0f &&
            x > 0.0f &&
            next_x > max_width)
        {
            x = 0.0f;

            lines += 1;
            column = 0;

            next_x = bc->xadvance;
        }

        x = next_x;

        column += 1;

        if (x > max_x)
            max_x = x;
    }

    if (s[s.len - 1] != '\n')
        lines += 1;

    return {
        max_x,
        lines * gfx_ctx.line_height
    };
}


funcdef void
gfx_push_clip(Quad rect)
{
	Clip_Node *node = alloc_struct(gfx_ctx.frame_arena, Clip_Node);

	s32 h = gfx_ctx.resolution.y;
	rect = gfx__rect_intersect(
		gfx_ctx.clip_stack->rect,
		rect
	);

	node->rect = rect;
	node->next = gfx_ctx.clip_stack;
	gfx_ctx.clip_stack = node;

	gfx__submit();
	gfx__apply_clip(rect, h);
}

funcdef void
gfx_pop_clip()
{
	assert(gfx_ctx.clip_stack);
    gfx__submit();

    gfx_ctx.clip_stack = gfx_ctx.clip_stack->next;

    if (gfx_ctx.clip_stack) {
        gfx__apply_clip(gfx_ctx.clip_stack->rect, gfx_ctx.resolution.y);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

funcdef f32 
line_height()
{
	return gfx_ctx.line_height;
}


funcdef f32 
delta_time()
{
	return gfx_ctx.delta_time;
}

funcdef f32
char_pixels(rune c)
{
    if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) c = NUM_CHARS + FIRST_CHAR - 1;
    f32 x = 0, y = 0;
    stbtt_aligned_quad q;
    stbtt_GetBakedQuad(gfx_ctx.baked_chars, ATLAS_SIZE, ATLAS_SIZE, (int)(c - FIRST_CHAR), &x, &y, &q, 1);
    return x;
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

funcdef Quad
gfx__rect_intersect(Quad a, Quad b)
{
    f32 x0 = Max(a.from.x, b.from.x);
    f32 y0 = Max(a.from.y, b.from.y);

    f32 x1 = Min(a.from.x + a.size.x, b.from.x + b.size.x);
    f32 y1 = Min(a.from.y + a.size.y, b.from.y + b.size.y);

    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;

    return {
        x0,
        y0,
        x1 - x0,
        y1 - y0
    };
}

funcdef void
gfx__apply_clip(Quad rect, s32 win_h)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(
        (int)rect.from.x,
        (int)(win_h - (rect.from.y + rect.size.y)),
        (int)rect.size.x,
        (int)rect.size.y
    );
}
