/*
xv6_tcc_layout.c

Combina .text, .rodata, .data y .bss de varios objetos ET_REL.
La idea sigue la colocacion de secciones de TinyCC tccelf.c, reducida a un
layout lineal con buffers fijos apropiados para xv6.

Donante conceptual:
  TinyCC tccelf.c
  commit d9d02c56401e43be43760b63f7d82f771a7ed1f6


*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_layout.h"

static int
append_section(struct Xv6TccElfBuffer *destination,
               const uchar *source, uint64 size, uint64 align,
               uint64 *base)
{
  uint offset;

  if(!destination || !source || !base || size > 0xffffffffULL ||
     align == 0 || align > 0xffffffffULL)
    return -1;
  if(xv6_tcc_section_add(destination, size, align, &offset) < 0)
    return -1;
  if(size)
    memmove(destination->data + offset, source, size);
  *base = offset;
  return 0;
}

static uint64
align_up64(uint64 value, uint64 align)
{
  return (value + align - 1) & ~(align - 1);
}

static void
update_max(uint *current, uint64 value)
{
  if(value > *current)
    *current = (uint)value;
}

int
xv6_tcc_layout_init(struct Xv6TccLinkLayout *layout,
                     struct Xv6TccElfBuffer *text,
                     struct Xv6TccElfBuffer *rodata,
                     struct Xv6TccElfBuffer *data)
{
  if(!layout || !text || !rodata || !data ||
     !text->data || !rodata->data || !data->data)
    return -1;

  memset(layout, 0, sizeof(*layout));
  layout->text = text;
  layout->rodata = rodata;
  layout->data = data;
  layout->text_align = 1;
  layout->rodata_align = 1;
  layout->data_align = 1;
  layout->bss_align = 1;
  text->size = 0;
  rodata->size = 0;
  data->size = 0;
  return 0;
}

int
xv6_tcc_layout_add_object(struct Xv6TccLinkLayout *layout,
                           const struct Xv6TccRelObjectView *view)
{
  struct Xv6TccObjectPlacement *placement;
  uint64 bss_align;

  if(!layout || !view || layout->finalized ||
     layout->input_count >= XV6_TCC_LAYOUT_MAX_INPUTS)
    return -1;

  placement = &layout->placements[layout->input_count];
  if(append_section(layout->text, view->text,
                    view->text_section->sh_size,
                    view->text_section->sh_addralign,
                    &placement->text_base) < 0 ||
     append_section(layout->rodata, view->rodata,
                    view->rodata_section->sh_size,
                    view->rodata_section->sh_addralign,
                    &placement->rodata_base) < 0 ||
     append_section(layout->data, view->data_bytes,
                    view->data_section->sh_size,
                    view->data_section->sh_addralign,
                    &placement->data_base) < 0)
    return -1;

  bss_align = view->bss_section->sh_addralign;
  placement->bss_base = align_up64(layout->bss_size, bss_align);
  if(view->bss_section->sh_size >
     0xffffffffffffffffULL - placement->bss_base)
    return -1;
  layout->bss_size = placement->bss_base + view->bss_section->sh_size;

  update_max(&layout->text_align, view->text_section->sh_addralign);
  update_max(&layout->rodata_align, view->rodata_section->sh_addralign);
  update_max(&layout->data_align, view->data_section->sh_addralign);
  update_max(&layout->bss_align, bss_align);
  layout->input_count++;
  return 0;
}

int
xv6_tcc_layout_finalize(struct Xv6TccLinkLayout *layout)
{
  uint64 end;

  if(!layout || !layout->text || !layout->rodata || !layout->data ||
     layout->input_count <= 0)
    return -1;

  layout->text_address = 0;
  end = layout->text->size;
  layout->rodata_address = align_up64(end, layout->rodata_align);
  if(layout->rodata->size >
     0xffffffffffffffffULL - layout->rodata_address)
    return -1;
  end = layout->rodata_address + layout->rodata->size;

  layout->data_address = align_up64(end, layout->data_align);
  if(layout->data->size >
     0xffffffffffffffffULL - layout->data_address)
    return -1;
  end = layout->data_address + layout->data->size;
  layout->file_size = end;

  layout->bss_address = align_up64(end, layout->bss_align);
  if(layout->bss_size >
     0xffffffffffffffffULL - layout->bss_address)
    return -1;
  layout->memory_size = layout->bss_address + layout->bss_size;
  layout->finalized = 1;
  return 0;
}

const struct Xv6TccObjectPlacement *
xv6_tcc_layout_placement_at(const struct Xv6TccLinkLayout *layout,
                             int index)
{
  if(!layout || index < 0 || index >= layout->input_count)
    return 0;
  return &layout->placements[index];
}
