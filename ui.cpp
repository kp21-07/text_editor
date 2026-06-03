#include "editor.h"

#define iterate_children(__p) for (UI_Box *child = __p->first; child; child = child->sibling)

global struct
{
	Arena *frame_arena;
	
	UI_Box *root;
	UI_Box *current;
	u64     count;

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
ui_init(Arena *frame_arena)
{
	ui_ctx.frame_arena = frame_arena;
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
ui_begin_frame(Rect rect, UI_Config frame_config)
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
	if (!box) return;

	UI_Config *cfg = &box->config;

	if (!(cfg->flags & UI_Invisible))
	{
		if (cfg->color != 0 || cfg->border >= 1.0f)
		{
			if (cfg->border >= 1.0f)
			{
				f32  b         = cfg->border;
				vec2 fg_pos    = { box->rect.from.x + b, box->rect.from.y + b };
				vec2 fg_size   = { box->rect.size.x - 2*b, box->rect.size.y - 2*b };
				f32  fg_radius = Max(0.0f, cfg->radius - b);

				if (cfg->radius > 0.5f) {
					draw_quad_rounded(box->rect.from, box->rect.size, cfg->radius,  cfg->border_color);
					draw_quad_rounded(fg_pos,         fg_size,        fg_radius,    cfg->color);
				} else {
					draw_quad(box->rect.from, box->rect.size, cfg->border_color);
					draw_quad(fg_pos,         fg_size,        cfg->color);
				}
			}
			else
			{
				if (cfg->radius > 0.5f) {
					draw_quad_rounded(box->rect.from, box->rect.size, cfg->radius, cfg->color);
				} else {
					draw_quad(box->rect.from, box->rect.size, cfg->color);
				}
			}
		}

		if (cfg->flags & UI_Clip_Children)
			gfx_push_clip(box->rect, ui_ctx.frame_arena);

		if (cfg->text.len > 0) {
			Rect inner = {
				{ box->rect.from.x + cfg->padding.left, box->rect.from.y + cfg->padding.top },
				{ box->rect.size.x - cfg->padding.left - cfg->padding.right,
				  box->rect.size.y - cfg->padding.top  - cfg->padding.bottom }
			};

			vec2 text_size = gfx_measure_text(cfg->text);
			vec2 text_pos  = {};

			switch (cfg->align) {
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

			draw_text(cfg->text, text_pos, cfg->text_color);
		}
	} else {
		if (cfg->flags & UI_Clip_Children)
			gfx_push_clip(box->rect, ui_ctx.frame_arena);
	}

	iterate_children(box) {
		ui__draw_box(child);
	}

	if (cfg->flags & UI_Clip_Children)
		gfx_pop_clip();
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

///////////////////////////////////
// components



funcdef void
ui_text(string text, u32 color, UI_Align alignment, UI_SizeKind x_kind)
{
	vec2 size = gfx_measure_text(text);

	UI_Config config = {};
	config.flags |= UI_Clip_Children;
	config.text = text;
	config.text_color = color;
	config.align = alignment;
	if (x_kind == Size_Fixed) {
		config.size = {
			{Size_Fixed, size.x},
			{Size_Fixed, size.y}
		};
	} else {
		config.size = {
			{Size_Fill, size.x},
			{Size_Fixed, size.y}
		};
	}

	UI(config);
}


funcdef void
ui_hr(u32 color, UI_SizeAxis size, f32 thick)
{
	UI_Config config = {};
	config.color = color;
	config.size = {
		size,
		{ Size_Fixed, thick }
	};
	UI(config);
}

