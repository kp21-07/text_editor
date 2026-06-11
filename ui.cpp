#include "editor.h"

const u64 MAX_KEY_BOXES = 4098;

#define iterate_children(__p) for (UI_Box *child = __p->first; child; child = child->sibling)

struct UI_State {
	string key;
	u64    frame;
};

global struct
{
	Arena *persist_arena;
	Arena *frame_arena;
	
	UI_Box *root;
	UI_Box *current;
	u64     count;

	slice<UI_State> storage;

} ui_ctx;

funcdef void
ui__append_child(UI_Box *parent, UI_Box *child)
{
	child->parent = parent;
	if (parent->first == nullptr) {
		parent->first = child;
		parent->last = child; return; 
	}

	parent->last->sibling = child;
	parent->last = child;
}

funcdef void
ui_init(Arena *persist, Arena *frame_arena)
{
	ui_ctx.persist_arena = persist;
	ui_ctx.frame_arena = frame_arena;

	slice<UI_State> state_storage = {};
	state_storage = alloc_slice(persist, UI_State, MAX_KEY_BOXES);
	ui_ctx.storage = state_storage;
}

funcdef UI_Box *
ui_open(UI_Config config)
{
	UI_Box *box = alloc_struct(ui_ctx.frame_arena, UI_Box);
	MemZeroStruct(box);

	if (ui_ctx.current != nullptr) 
		ui__append_child(ui_ctx.current, box);
	else 
		ui_ctx.root = box; 
	
	ui_ctx.current = box;
	ui_ctx.count += 1;
	ui_ctx.current->config = config;

	return box;
}


funcdef UI_Box *
ui_open_key(UI_Config config, string key)
{
	UI_Box *box = ui_open(config);
	u64 key_hash = hash_string(key);

	auto table = ui_ctx.storage;
	u64 capacity = table.len;
	u64 index = key_hash % capacity;

	for (u64 i=0; i<capacity; ++i) {
	}

	return box;
}

funcdef void
ui_close()
{
	if (!ui_ctx.current)
		return;

	UI_Box *box = ui_ctx.current;

	f32 content_w = 0;
	f32 content_h = 0;

	if (box->config.layout == Layout_Row) {
		f32 max_h = 0;
		f32 sum_w = 0;

		iterate_children(box) {
			if (child->config.size.w.kind == Size_Fixed)
				child->rect.size.x = child->config.size.w.value;
			if (child->config.size.h.kind == Size_Fixed)
				child->rect.size.y = child->config.size.h.value;

			sum_w += child->rect.size.x;
			max_h  = Max(max_h, child->rect.size.y);
		}

		content_w = sum_w;
		content_h = max_h;
	} else {
		f32 max_w = 0;
		f32 sum_h = 0;

		iterate_children(box) {
			if (child->config.size.w.kind == Size_Fixed)
				child->rect.size.x = child->config.size.w.value;
			if (child->config.size.h.kind == Size_Fixed)
				child->rect.size.y = child->config.size.h.value;

			sum_h += child->rect.size.y;
			max_w  = Max(max_w, child->rect.size.x);
		}

		content_w = max_w;
		content_h = sum_h;
	}

	content_w += box->config.padding.left + box->config.padding.right;
	content_h += box->config.padding.top  + box->config.padding.bottom;

	if (box->config.size.w.kind == Size_Fit) { box->rect.size.x = content_w; }
	if (box->config.size.h.kind == Size_Fit) { box->rect.size.y = content_h; }

	ui_ctx.current = ui_ctx.current->parent;
}


funcdef void
ui_begin_frame(Quad rect, UI_Config frame_config)
{
	UI_Box *root = ui_open({});
	root->rect = rect;

	UI_Config config = frame_config;
	config.size = {
		{ Size_Fixed, rect.size.x },
		{ Size_Fixed, rect.size.y },
	};
	root->config = config;
}

funcdef void
ui__layout_positions(UI_Box *box)
{
	if (!box)
		return;

	UI_Config *config = &box->config;

	f32 x = box->rect.from.x + config->padding.left;
	f32 y = box->rect.from.y + config->padding.top;

	f32 inner_w = box->rect.size.x - config->padding.left - config->padding.right;
	f32 inner_h = box->rect.size.y - config->padding.top - config->padding.bottom;

	u64 child_count = 0;
	iterate_children(box) {
		child_count += 1;
	}

	u64 gap_count = child_count > 0 ? child_count - 1 : 0;

	f32 used = 0.0f;
	f32 fill_total = 0.0f;

	if (config->layout == Layout_Row) {

		iterate_children(box)
		{
			UI_SizeAxis size = child->config.size.w;
			switch (size.kind) {
				case Size_Fixed:   used += size.value;                  break;
				case Size_Percent: used += inner_w * size.value;        break;
				case Size_Fit:     used += child->rect.size.x;          break;
				case Size_Fill:    fill_total += size.value;            break;
			}
		}

		used += config->gap * gap_count;
		f32 remaining = Max(0, inner_w - used);
		f32 cursor    = x;

		iterate_children(box)
		{
			UI_SizeAxis w = child->config.size.w;
			UI_SizeAxis h = child->config.size.h;

			switch (w.kind) {
				case Size_Fixed:   child->rect.size.x = w.value;                          break;
				case Size_Percent: child->rect.size.x = inner_w * w.value;                break;
				case Size_Fill:    child->rect.size.x = remaining * (w.value/fill_total); break;
				case Size_Fit:                                                            break;
			}

			switch (h.kind) {
				case Size_Fixed:   child->rect.size.y = h.value;           break;
				case Size_Percent: child->rect.size.y = inner_h * h.value; break;
				case Size_Fill:    child->rect.size.y = inner_h;           break;
				case Size_Fit:                                             break;
			}

			child->rect.from.x = cursor;
			switch (config->align) {
				case Align_Start:  child->rect.from.y = y;                                        break;
				case Align_Center: child->rect.from.y = y + (inner_h - child->rect.size.y)*0.5f;  break;
				case Align_End:    child->rect.from.y = y + (inner_h - child->rect.size.y);       break;
			}

			cursor += child->rect.size.x + config->gap;
			ui__layout_positions(child);
		}
	} else {
		iterate_children(box)
		{
			UI_SizeAxis size = child->config.size.h;
			switch (size.kind) {
				case Size_Fixed:   used += size.value;                  break;
				case Size_Percent: used += inner_h * size.value;        break;
				case Size_Fit:     used += child->rect.size.y;          break;
				case Size_Fill:    fill_total += size.value;            break;
			}
		}

		used += config->gap * gap_count;
		f32 remaining = Max(0, inner_h - used);
		f32 cursor    = y;

		iterate_children(box)
		{
			UI_SizeAxis w = child->config.size.w;
			UI_SizeAxis h = child->config.size.h;

			switch (w.kind) {
				case Size_Fixed:   child->rect.size.x = w.value;           break;
				case Size_Percent: child->rect.size.x = inner_w * w.value; break;
				case Size_Fill:    child->rect.size.x = inner_w;           break;
				case Size_Fit:                                             break;
			}

			switch (h.kind) {
				case Size_Fixed:   child->rect.size.y = h.value;                          break;
				case Size_Percent: child->rect.size.y = inner_h * h.value;                break;
				case Size_Fill:    child->rect.size.y = remaining * (h.value/fill_total); break;
				case Size_Fit:                                                            break;
			}

			child->rect.from.y = cursor;
			switch (config->align) {
				case Align_Start:  child->rect.from.x = x;                                        break;
				case Align_Center: child->rect.from.x = x + (inner_w - child->rect.size.x)*0.5f;  break;
				case Align_End:    child->rect.from.x = x + (inner_w - child->rect.size.x);       break;
			}

			cursor += child->rect.size.y + config->gap;
			ui__layout_positions(child);
		}
	}
}

funcdef void
ui_end_frame()
{
	ui_close();

	ui__layout_positions(ui_ctx.root);
}

funcdef void
ui__draw_box(UI_Box *box)
{
	if (!box)
		return;

	const UI_Config config = box->config;
	UI_Flags flags = config.flags;

	if (flags & UI_Drop_Shadow)
	{
		Quad rect = box->rect;

		vec2 from = { rect.from.x - 20, rect.from.y - 20 };
		vec2 size = { rect.size.x + 40, rect.size.y + 40 };

		f32 net_radius = config.radius + 16;
		gfx_draw_quad({from.x, from.y, size.x, size.y}, {}, color(0x00000044), net_radius, net_radius);
	}

	 if (!(flags & UI_Invisible)) 
	 {
		 f32 b          = config.border;
		 vec2 fg_pos    = { box->rect.from.x + b, box->rect.from.y + b };
		 vec2 fg_size   = { box->rect.size.x - 2*b, box->rect.size.y - 2*b };
		 f32  fg_radius = Max(0.0f, config.radius - b);

		 if (config.border >= 1.0f) {
			 vec2 out_pos = box->rect.from;
			 vec2 out_size = box->rect.size;
			 gfx_draw_quad({out_pos.x, out_pos.y, out_size.x, out_size.y}, {}, config.border_color, config.radius);
		 }
		 gfx_draw_quad({fg_pos.x, fg_pos.y, fg_size.x, fg_size.y}, {}, config.fill_color, fg_radius);
	 }

	 Quad inner = {
		 {
			 box->rect.from.x + config.padding.left,
			 box->rect.from.y + config.padding.top
		 },
		 {
			 box->rect.size.x - config.padding.left - config.padding.right,
			 box->rect.size.y - config.padding.top  - config.padding.bottom
		 }
	 };

	if (flags & UI_Clip_Children) {
		gfx_push_clip(inner);
	}

	if (flags & UI_Draw_Text)
	{
		vec2 text_size = gfx_measure_text(config.text);
		vec2 text_pos  = {};

		switch (config.align) {
			case Align_Start: {
				text_pos = {
					inner.from.x,
					inner.from.y + (inner.size.y - text_size.y) * 0.5f,
				};
			} break;
			case Align_Center: {
			    text_pos = {
			 	   inner.from.x + (inner.size.x - text_size.x) * 0.5f,
			 	   inner.from.y + (inner.size.y - text_size.y) * 0.5f,
			    };
			} break;
			case Align_End: {
				text_pos = {
					inner.from.x + inner.size.x - text_size.x,
					inner.from.y + (inner.size.y - text_size.y) * 0.5f,
				};
			} break;
		}

		gfx_draw_text(config.text, text_pos, config.text_color); 
	}

	iterate_children(box) {
		ui__draw_box(child);
	}

	if (flags & UI_Clip_Children) {
		gfx_pop_clip();
	}
}

funcdef void
ui_draw()
{
	ui__draw_box(ui_ctx.root);

	ui_ctx.root = nullptr;
	ui_ctx.current = nullptr;
	ui_ctx.count = 0;
}


funcdef UI_Box *
ui_ctx_current()
{
	return ui_ctx.current;
}

///////////////////////////

funcdef UI_Config
gap(UI_Size size)
{
	UI_Config cfg = {};
	cfg.flags = UI_Invisible;
	cfg.size = size;
	return cfg;
}

funcdef UI_Config
label(string s, vec4 color, UI_SizeKind x_kind, UI_Align align)
{
	UI_Config cfg = {};
	cfg.flags = UI_Invisible | UI_Draw_Text | UI_Clip_Children;
	cfg.text = s;
	cfg.text_color = color;
	cfg.align = align;

	vec2 dims = gfx_measure_text(s);
	cfg.size = {
		x_kind == Size_Fixed ? size_fixed(dims.x) : size_fill(1.0),
		size_fixed(Max(dims.y, line_height()))
	};
	return cfg;
}
