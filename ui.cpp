#include "editor.h"
#include "config.h"

#define iterate_children(__p) for (UI_Box *child = __p->first; child; child = child->sibling)

global struct {
    Arena  *frame_arena;
    UI_Box *root;
    UI_Box *current;
    u64     box_count;
} ui;

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
	ui.frame_arena = frame_arena;
}

funcdef UI_Box *
ui_open()
{
	UI_Box *box = alloc_struct(ui.frame_arena, UI_Box);
	MemZeroStruct(box);

	if (ui.current != nullptr) 
		ui__append_child(ui.current, box);
	else 
		ui.root = box; 
	
	ui.current = box;
	ui.box_count += 1;
	return box;
}

funcdef void
ui_set_config(UI_Config config)
{
	ui.current->config = config;
}

funcdef void
ui_close()
{
	if (!ui.current) return;

	UI_Box *box = ui.current;

	////////////////////////////
	// ~geb: resolve the size
	// all the children have closed,
	// thus their sizes are also resolved

	f32 content_w = 0;
	f32 content_h = 0;

	if (box->config.layout == Layout_Row) {
		f32 max_h = 0;
		f32 sum_w = 0;

		iterate_children(box) {
			sum_w += child->rect.size.x;
			max_h = Max(max_h, child->rect.size.y);
		}

        content_w = sum_w;
        content_h = max_h;
	} else {
		f32 max_w = 0;
		f32 sum_h = 0;

		iterate_children(box) {
			sum_h += child->rect.size.y;
			max_w = Max(max_w, child->rect.size.x);
		}

		content_w = max_w;
		content_h = sum_h;
	}

    content_w += box->config.padding.left + box->config.padding.right;
    content_h += box->config.padding.top + box->config.padding.bottom;

	if (box->config.size.w.kind == Size_Fit) { box->rect.size.x = content_w; }
	if (box->config.size.h.kind == Size_Fit) { box->rect.size.y = content_h; }

	ui.current = ui.current->parent;
}

funcdef void
ui_begin_frame(Rect rect, UI_Flags flags, UI_Layout layout, UI_Padding pad)
{
	UI_Box *root = ui_open();
	root->rect = rect;

	UI_Config config = {};
	config.size = {
		{ Size_Fixed, rect.size.x },
		{ Size_Fixed, rect.size.x },
	};
	config.padding = pad;
	config.layout  = layout;
	config.color   = Hex(0x000000FF);
	config.flags   = flags;

	ui_set_config(config);
}

funcdef void
ui__layout_position(UI_Box *box)
{
	if (!box) return;

	UI_Config *config = &box->config;

	f32 x = box->rect.from.x + config->padding.left;
	f32 y = box->rect.from.y + config->padding.top;

	f32 inner_w =
		box->rect.size.x
		- config->padding.left
		- config->padding.right;

	f32 inner_h =
		box->rect.size.y
		- config->padding.top
		- config->padding.bottom;

	f32 used = 0;
	f32 fill_total = 0;

	if (config->layout == Layout_Row) {

		iterate_children(box)
		{
			UI_Size_Axis size = child->config.size.w;

			switch (size.kind) {

			case Size_Fixed:
				used += size.value;
				break;

			case Size_Percent:
				used += inner_w * size.value;
				break;

			case Size_Fit:
				used += child->rect.size.x;
				break;

			case Size_Fill:
				fill_total += size.value;
				break;
			}
		}

		used += config->gap * Max(0, (s32)ui.box_count - 1);

		f32 remaining = Max(0, inner_w - used);

		////////////////////////////////////////
		// ~geb: place children

		f32 cursor = x;

		iterate_children(box)
		{
			UI_Size_Axis w = child->config.size.w;
			UI_Size_Axis h = child->config.size.h;

			////////////////////////////////////
			// width

			switch (w.kind) {

			case Size_Fixed:
				child->rect.size.x = w.value;
				break;

			case Size_Percent:
				child->rect.size.x = inner_w * w.value;
				break;

			case Size_Fill:
				child->rect.size.x =
					remaining * (w.value / fill_total);
				break;

			case Size_Fit:
				break;
			}

			////////////////////////////////////
			// height

			switch (h.kind) {
			case Size_Fixed:
				child->rect.size.y = h.value;
				break;

			case Size_Percent:
				child->rect.size.y = inner_h * h.value;
				break;

			case Size_Fill:
				child->rect.size.y = inner_h;
				break;

			case Size_Fit:
				break;
			}

			////////////////////////////////////
			// position

			child->rect.from.x = cursor;

			switch (config->align) {

			case Align_Start:
				child->rect.from.y = y;
				break;

			case Align_Center:
				child->rect.from.y = y + (inner_h - child->rect.size.y) * 0.5f;
				break;

			case Align_End:
				child->rect.from.y = y + (inner_h - child->rect.size.y);
				break;
			}

			cursor += child->rect.size.x + config->gap;

			ui__layout_position(child);
		}
	}
	else {

		////////////////////////////////////////
		// column layout

		iterate_children(box)
		{
			UI_Size_Axis size = child->config.size.h;

			switch (size.kind) {

			case Size_Fixed:
				used += size.value;
				break;

			case Size_Percent:
				used += inner_h * size.value;
				break;

			case Size_Fit:
				used += child->rect.size.y;
				break;

			case Size_Fill:
				fill_total += size.value;
				break;
			}
		}

		f32 remaining = Max(0, inner_h - used);

		f32 cursor = y;

		iterate_children(box)
		{
			UI_Size_Axis w = child->config.size.w;
			UI_Size_Axis h = child->config.size.h;

			////////////////////////////////////
			// width

			switch (w.kind) {

			case Size_Fixed:
				child->rect.size.x = w.value;
				break;

			case Size_Percent:
				child->rect.size.x = inner_w * w.value;
				break;

			case Size_Fill:
				child->rect.size.x = inner_w;
				break;

			case Size_Fit:
				break;
			}

			////////////////////////////////////
			// height

			switch (h.kind) {

			case Size_Fixed:
				child->rect.size.y = h.value;
				break;

			case Size_Percent:
				child->rect.size.y = inner_h * h.value;
				break;

			case Size_Fill:
				child->rect.size.y =
					remaining * (h.value / fill_total);
				break;

			case Size_Fit:
				break;
			}

			////////////////////////////////////
			// position

			child->rect.from.y = cursor;

			switch (config->align) {

			case Align_Start:
				child->rect.from.x = x;
				break;

			case Align_Center:
				child->rect.from.x =
					x + (inner_w - child->rect.size.x) * 0.5f;
				break;

			case Align_End:
				child->rect.from.x =
					x + (inner_w - child->rect.size.x);
				break;
			}

			cursor += child->rect.size.y + config->gap;

			ui__layout_position(child);
		}
	}
}

funcdef UI_Draw
ui__make_draw (UI_Box *box)
{
	UI_Draw result = {};
	result.rect    = box->rect;
	result.border  = box->config.border;
	result.radius  = box->config.radius;
	result.color   = box->config.color;
	result.border_color = box->config.border_color;
	result.text    = box->config.text;
	result.flags   = box->config.flags;
	result.align   = box->config.align;

	return result;
}

funcdef void
ui__build_draw_list(List<UI_Draw> *list, UI_Box *box)
{
	if (!box) return;

	UI_Flags flags = box->config.flags;

	if (!(flags & UI_Invisible))
		append(list, ui__make_draw(box));

	iterate_children(box) {
		ui__build_draw_list(list, child);
	}
}

funcdef UI_Draw_List
ui_end_frame()
{
	ui__layout_position(ui.root);

	auto buffer = alloc_slice(ui.frame_arena, UI_Draw, ui.box_count);
	auto list = list_from_buffer(buffer);

	ui__build_draw_list(&list, ui.root);

	ui.root = nullptr;
	ui.current = nullptr;
	ui.box_count = 0;

	return slice_from_list(list);
}


funcdef UI_Box *
ui_current() 
{
	return ui.current;
}

funcdef void
ui_draw_cmd_list(UI_Draw_List list)
{
	UI_Draw_List draw_list = list;
	for (u64 i = 0; i < draw_list.len; ++i) {
		UI_Draw draw = draw_list[i];

		if (draw.text.len > 0) {
			graphics_push_clip(draw.rect, ui.frame_arena);
			defer(graphics_pop_clip());

			vec2 text_size = graphics_measure_text(draw.text);

			vec2 pos = draw.rect.from;

			switch (draw.align) {
			case Align_Start:
				pos.x = draw.rect.from.x;
				break;

			case Align_Center:
				pos.x = draw.rect.from.x + (draw.rect.size.x - text_size.x) * 0.5f;
				break;

			case Align_End:
				pos.x = draw.rect.from.x + (draw.rect.size.x - text_size.x);
				break;
			}

			pos.y = draw.rect.from.y + (draw.rect.size.y - text_size.y) * 0.5f;

			draw_text(draw.text, pos, draw.color);
			continue;
		}
		if (draw.border > 0.5f) {
			draw_quad_rounded(
				draw.rect.from,
				draw.rect.size,
				draw.radius,
				draw.border_color
			);
		}

		vec2 pos = {
			draw.rect.from.x + draw.border,
			draw.rect.from.y + draw.border
		};

		vec2 size = {
			draw.rect.size.x - draw.border * 2,
			draw.rect.size.y - draw.border * 2
		};

		draw_quad_rounded(
			pos,
			size,
			draw.radius - draw.border,
			draw.color
		);
	}
}

#undef iterate_children
