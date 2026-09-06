/*
xv6_tcc_link.c

Tabla global de simbolos fuertes/debiles y relocaciones de codigo/datos.
La organizacion sigue conceptualmente la resolucion de simbolos de tccelf.c
y el despacho de relocaciones de riscv64-link.c de TinyCC, sustituyendo hashes
y estructuras dinamicas por arrays de capacidad fija adecuados para xv6.

Donante conceptual:
  TinyCC tccelf.c y riscv64-link.c
  commit d9d02c56401e43be43760b63f7d82f771a7ed1f6

*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_link.h"

static int
symbol_binding(const struct Xv6TccElfSym *symbol)
{
  return (symbol->st_info >> 4) & 0xf;
}

static void
set_error_symbol(struct Xv6TccLinkState *state, const char *name)
{
  int length;

  state->error_symbol[0] = 0;
  if(!name)
    return;
  length = strlen(name);
  if(length >= (int)sizeof(state->error_symbol))
    length = sizeof(state->error_symbol) - 1;
  memmove(state->error_symbol, name, length);
  state->error_symbol[length] = 0;
}

static int
find_global_slot(const struct Xv6TccLinkState *state, const char *name)
{
  int i;

  for(i = 0; i < state->global_count; i++)
    if(strcmp(state->globals[i].name, name) == 0)
      return i;
  return -1;
}

static int
get_or_create_global(struct Xv6TccLinkState *state,
                     const char *name, int *slot)
{
  struct Xv6TccGlobalSymbol *global;
  int found;
  int length;

  found = find_global_slot(state, name);
  if(found >= 0){
    *slot = found;
    return XV6_TCC_LINK_OK;
  }

  if(state->global_count >= XV6_TCC_LINK_MAX_GLOBALS)
    return XV6_TCC_LINK_TOO_MANY_GLOBALS;
  length = strlen(name);
  if(length <= 0 || length >= (int)sizeof(state->globals[0].name))
    return XV6_TCC_LINK_BAD_SYMBOL;

  *slot = state->global_count;
  global = &state->globals[state->global_count];
  state->global_count++;
  memset(global, 0, sizeof(*global));
  memmove(global->name, name, length + 1);
  global->object_index = -1;
  return XV6_TCC_LINK_OK;
}

static int
section_value(const struct Xv6TccLinkState *state,
              int object_index,
              const struct Xv6TccElfSym *symbol,
              ushort *section_index,
              uint64 *value)
{
  const struct Xv6TccObjectPlacement *placement;
  uint64 base;

  if(object_index < 0 || object_index >= state->input_count ||
     !symbol || !section_index || !value)
    return XV6_TCC_LINK_BAD_SYMBOL;

  placement = xv6_tcc_layout_placement_at(state->layout, object_index);
  if(!placement)
    return XV6_TCC_LINK_BAD_SYMBOL;

  if(symbol->st_shndx == XV6_TCC_SHN_TEXT)
    base = state->layout->text_address + placement->text_base;
  else if(symbol->st_shndx == XV6_TCC_SHN_RODATA)
    base = state->layout->rodata_address + placement->rodata_base;
  else if(symbol->st_shndx == XV6_TCC_SHN_DATA)
    base = state->layout->data_address + placement->data_base;
  else if(symbol->st_shndx == XV6_TCC_SHN_BSS)
    base = state->layout->bss_address + placement->bss_base;
  else
    return XV6_TCC_LINK_BAD_SYMBOL;

  if(symbol->st_value > 0xffffffffffffffffULL - base)
    return XV6_TCC_LINK_BAD_SYMBOL;
  *section_index = symbol->st_shndx;
  *value = base + symbol->st_value;
  return XV6_TCC_LINK_OK;
}

static int
collect_globals(struct Xv6TccLinkState *state)
{
  int object_index;

  for(object_index = 0; object_index < state->input_count; object_index++){
    const struct Xv6TccRelObjectView *view;
    uint symbol_index;

    view = &state->views[object_index];
    for(symbol_index = 1; symbol_index < view->symbol_count;
        symbol_index++){
      const struct Xv6TccElfSym *symbol;
      const char *name;
      struct Xv6TccGlobalSymbol *global;
      ushort section_index;
      uint64 value;
      int binding;
      int slot;
      int status;

      symbol = xv6_tcc_rel_symbol_at(view, symbol_index);
      if(!symbol)
        continue;
      binding = symbol_binding(symbol);
      if(binding != XV6_TCC_STB_GLOBAL && binding != XV6_TCC_STB_WEAK)
        continue;

      name = xv6_tcc_rel_symbol_name(view, symbol);
      if(!name || !name[0]){
        state->error_object = object_index;
        return XV6_TCC_LINK_BAD_SYMBOL;
      }

      status = get_or_create_global(state, name, &slot);
      if(status != XV6_TCC_LINK_OK){
        set_error_symbol(state, name);
        state->error_object = object_index;
        return status;
      }
      global = &state->globals[slot];

      if(symbol->st_shndx == XV6_TCC_SHN_UNDEF){
        if(binding == XV6_TCC_STB_GLOBAL)
          global->strong_reference = 1;
        continue;
      }

      status = section_value(state, object_index, symbol,
                             &section_index, &value);
      if(status != XV6_TCC_LINK_OK){
        set_error_symbol(state, name);
        state->error_object = object_index;
        return status;
      }

      if(global->defined){
        if(!global->weak_definition && binding == XV6_TCC_STB_GLOBAL){
          set_error_symbol(state, name);
          state->error_object = object_index;
          return XV6_TCC_LINK_DUPLICATE_SYMBOL;
        }
        if(!global->weak_definition && binding == XV6_TCC_STB_WEAK)
          continue;
        if(global->weak_definition && binding == XV6_TCC_STB_WEAK)
          continue;
        /* Una definición fuerte reemplaza a una débil anterior. */
      }

      global->defined = 1;
      global->weak_definition = binding == XV6_TCC_STB_WEAK;
      global->object_index = object_index;
      global->symbol_index = symbol_index;
      global->section_index = section_index;
      global->value = value;
    }
  }

  for(object_index = 0; object_index < state->global_count; object_index++){
    if(state->globals[object_index].strong_reference &&
       !state->globals[object_index].defined){
      set_error_symbol(state, state->globals[object_index].name);
      return XV6_TCC_LINK_UNDEFINED_SYMBOL;
    }
  }
  return XV6_TCC_LINK_OK;
}

static int
add_signed_value(uint64 value, long addend, uint64 *result)
{
  if(addend >= 0){
    if((uint64)addend > 0xffffffffffffffffULL - value)
      return -1;
    *result = value + (uint64)addend;
  } else {
    uint64 magnitude;

    magnitude = (uint64)(-(addend + 1)) + 1;
    if(magnitude > value)
      return -1;
    *result = value - magnitude;
  }
  return 0;
}

static int
resolve_relocation_symbol(struct Xv6TccLinkState *state,
                          int object_index,
                          const struct Xv6TccElfSym *symbol,
                          ushort *section_index,
                          uint64 *value)
{
  int binding;

  if(symbol->st_shndx != XV6_TCC_SHN_UNDEF)
    return section_value(state, object_index, symbol,
                         section_index, value);

  binding = symbol_binding(symbol);
  if(binding == XV6_TCC_STB_GLOBAL || binding == XV6_TCC_STB_WEAK){
    const struct Xv6TccGlobalSymbol *global;
    const char *name;

    name = xv6_tcc_rel_symbol_name(&state->views[object_index], symbol);
    global = xv6_tcc_link_find_global(state, name);
    if(global && global->defined){
      *section_index = global->section_index;
      *value = global->value;
      return XV6_TCC_LINK_OK;
    }
    if(binding == XV6_TCC_STB_WEAK){
      *section_index = XV6_TCC_SHN_UNDEF;
      *value = 0;
      return XV6_TCC_LINK_OK;
    }
    set_error_symbol(state, name);
    return XV6_TCC_LINK_UNDEFINED_SYMBOL;
  }

  return XV6_TCC_LINK_BAD_SYMBOL;
}

static int
map_relocation_status(int status)
{
  if(status == XV6_TCC_RELOC_RANGE)
    return XV6_TCC_LINK_RELOCATION_RANGE;
  if(status == XV6_TCC_RELOC_ALIGNMENT)
    return XV6_TCC_LINK_RELOCATION_ALIGNMENT;
  if(status == XV6_TCC_RELOC_BOUNDS)
    return XV6_TCC_LINK_RELOCATION_BOUNDS;
  if(status == XV6_TCC_RELOC_UNSUPPORTED)
    return XV6_TCC_LINK_RELOCATION_UNSUPPORTED;
  return XV6_TCC_LINK_BAD_RELOCATION;
}

static int
apply_relocation_pass(struct Xv6TccLinkState *state,
                      struct Xv6TccElfBuffer *combined_text,
                      int high_pass)
{
  int object_index;

  for(object_index = 0; object_index < state->input_count; object_index++){
    const struct Xv6TccRelObjectView *view;
    const struct Xv6TccObjectPlacement *placement;
    uint relocation_index;

    view = &state->views[object_index];
    placement = xv6_tcc_layout_placement_at(state->layout, object_index);
    if(!placement)
      return XV6_TCC_LINK_INVALID;

    for(relocation_index = 0;
        relocation_index < view->relocation_count;
        relocation_index++){
      const struct Xv6TccElfRela *relocation;
      const struct Xv6TccElfSym *symbol;
      const char *symbol_name;
      uint symbol_index;
      uint type;
      uint64 patch_offset;
      uint64 place;
      uint64 target;
      ushort target_section;
      int status;

      relocation = xv6_tcc_rel_relocation_at(view, relocation_index);
      if(!relocation)
        return XV6_TCC_LINK_BAD_RELOCATION;
      type = xv6_tcc_elf_r_type(relocation->r_info);
      if((type == XV6_TCC_R_RISCV_PCREL_HI20) != high_pass)
        continue;

      symbol_index = xv6_tcc_elf_r_symbol(relocation->r_info);
      symbol = xv6_tcc_rel_symbol_at(view, symbol_index);
      if(!symbol)
        return XV6_TCC_LINK_BAD_RELOCATION;
      symbol_name = xv6_tcc_rel_symbol_name(view, symbol);

      status = resolve_relocation_symbol(state, object_index, symbol,
                                         &target_section, &target);
      if(status != XV6_TCC_LINK_OK){
        set_error_symbol(state, symbol_name);
        state->error_object = object_index;
        state->error_relocation = relocation_index;
        return status;
      }

      if((type == XV6_TCC_R_RISCV_BRANCH ||
          type == XV6_TCC_R_RISCV_JAL ||
          type == XV6_TCC_R_RISCV_CALL) &&
         target_section != XV6_TCC_SHN_TEXT &&
         target_section != XV6_TCC_SHN_UNDEF){
        set_error_symbol(state, symbol_name);
        state->error_object = object_index;
        state->error_relocation = relocation_index;
        return XV6_TCC_LINK_BAD_RELOCATION;
      }

      if(relocation->r_offset >
         0xffffffffffffffffULL - placement->text_base)
        return XV6_TCC_LINK_BAD_RELOCATION;
      patch_offset = placement->text_base + relocation->r_offset;
      place = state->layout->text_address + patch_offset;
      if(patch_offset > 0xffffffffULL)
        return XV6_TCC_LINK_RELOCATION_BOUNDS;
      if(add_signed_value(target, relocation->r_addend, &target) < 0)
        return XV6_TCC_LINK_BAD_RELOCATION;

      status = xv6_tcc_apply_relocation(
          combined_text->data, combined_text->size,
          (uint)patch_offset, type, place, target,
          &state->pcrel_table);
      if(status != XV6_TCC_RELOC_OK){
        set_error_symbol(state, symbol_name);
        state->error_object = object_index;
        state->error_relocation = relocation_index;
        return map_relocation_status(status);
      }
      state->applied_relocations++;
    }
  }
  return XV6_TCC_LINK_OK;
}

static int
apply_data_relocation_table(struct Xv6TccLinkState *state,
                            int object_index,
                            const struct Xv6TccElfRela *relocations,
                            uint relocation_count,
                            struct Xv6TccElfBuffer *combined,
                            uint64 placement_base,
                            uint64 section_address)
{
  const struct Xv6TccRelObjectView *view;
  uint relocation_index;

  view = &state->views[object_index];
  for(relocation_index = 0; relocation_index < relocation_count;
      relocation_index++){
    const struct Xv6TccElfRela *relocation;
    const struct Xv6TccElfSym *symbol;
    const char *symbol_name;
    uint symbol_index;
    uint type;
    uint64 patch_offset;
    uint64 place;
    uint64 target;
    ushort target_section;
    int status;

    relocation = &relocations[relocation_index];
    type = xv6_tcc_elf_r_type(relocation->r_info);
    symbol_index = xv6_tcc_elf_r_symbol(relocation->r_info);
    symbol = xv6_tcc_rel_symbol_at(view, symbol_index);
    if(!symbol)
      return XV6_TCC_LINK_BAD_RELOCATION;
    symbol_name = xv6_tcc_rel_symbol_name(view, symbol);

    status = resolve_relocation_symbol(state, object_index, symbol,
                                       &target_section, &target);
    if(status != XV6_TCC_LINK_OK){
      set_error_symbol(state, symbol_name);
      state->error_object = object_index;
      state->error_relocation = relocation_index;
      return status;
    }
    (void)target_section;

    if(relocation->r_offset >
       0xffffffffffffffffULL - placement_base)
      return XV6_TCC_LINK_BAD_RELOCATION;
    patch_offset = placement_base + relocation->r_offset;
    place = section_address + patch_offset;
    if(patch_offset > 0xffffffffULL)
      return XV6_TCC_LINK_RELOCATION_BOUNDS;
    if(add_signed_value(target, relocation->r_addend, &target) < 0)
      return XV6_TCC_LINK_BAD_RELOCATION;

    status = xv6_tcc_apply_relocation(
        combined->data, combined->size, (uint)patch_offset,
        type, place, target, &state->pcrel_table);
    if(status != XV6_TCC_RELOC_OK){
      set_error_symbol(state, symbol_name);
      state->error_object = object_index;
      state->error_relocation = relocation_index;
      return map_relocation_status(status);
    }
    state->applied_relocations++;
  }
  return XV6_TCC_LINK_OK;
}

static int
apply_all_relocations(struct Xv6TccLinkState *state,
                      struct Xv6TccElfBuffer *combined_text)
{
  int object_index;
  int status;

  state->pcrel_table.entries = state->pcrel_entries;
  state->pcrel_table.count = 0;
  state->pcrel_table.capacity = XV6_TCC_LINK_MAX_PCREL;

  status = apply_relocation_pass(state, combined_text, 1);
  if(status != XV6_TCC_LINK_OK)
    return status;
  status = apply_relocation_pass(state, combined_text, 0);
  if(status != XV6_TCC_LINK_OK)
    return status;

  for(object_index = 0; object_index < state->input_count; object_index++){
    const struct Xv6TccRelObjectView *view;
    const struct Xv6TccObjectPlacement *placement;

    view = &state->views[object_index];
    placement = xv6_tcc_layout_placement_at(state->layout, object_index);
    if(!placement)
      return XV6_TCC_LINK_INVALID;

    status = apply_data_relocation_table(
        state, object_index, view->rodata_relocations,
        view->rodata_relocation_count, state->layout->rodata,
        placement->rodata_base, state->layout->rodata_address);
    if(status != XV6_TCC_LINK_OK)
      return status;
    status = apply_data_relocation_table(
        state, object_index, view->data_relocations,
        view->data_relocation_count, state->layout->data,
        placement->data_base, state->layout->data_address);
    if(status != XV6_TCC_LINK_OK)
      return status;
  }
  return XV6_TCC_LINK_OK;
}

int
xv6_tcc_link_resolve(struct Xv6TccLinkState *state,
                      const struct Xv6TccRelObjectView *views,
                      int input_count,
                      const struct Xv6TccLinkLayout *layout,
                      struct Xv6TccElfBuffer *combined_text)
{
  int status;

  if(!state || !views || input_count <= 0 ||
     input_count > XV6_TCC_LAYOUT_MAX_INPUTS ||
     !layout || !layout->finalized ||
     layout->input_count != input_count ||
     !layout->rodata || !layout->rodata->data ||
     !layout->data || !layout->data->data ||
     !combined_text || !combined_text->data)
    return XV6_TCC_LINK_INVALID;

  memset(state, 0, sizeof(*state));
  state->views = views;
  state->layout = layout;
  state->input_count = input_count;
  state->error_object = -1;
  state->error_relocation = -1;

  status = collect_globals(state);
  if(status != XV6_TCC_LINK_OK)
    return status;
  return apply_all_relocations(state, combined_text);
}

const struct Xv6TccGlobalSymbol *
xv6_tcc_link_find_global(const struct Xv6TccLinkState *state,
                          const char *name)
{
  int slot;

  if(!state || !name)
    return 0;
  slot = find_global_slot(state, name);
  if(slot < 0)
    return 0;
  return &state->globals[slot];
}

const char *
xv6_tcc_link_status_text(int status)
{
  if(status == XV6_TCC_LINK_OK)
    return "sin error";
  if(status == XV6_TCC_LINK_INVALID)
    return "estado de enlace invalido";
  if(status == XV6_TCC_LINK_TOO_MANY_GLOBALS)
    return "demasiados simbolos globales";
  if(status == XV6_TCC_LINK_DUPLICATE_SYMBOL)
    return "simbolo global duplicado";
  if(status == XV6_TCC_LINK_UNDEFINED_SYMBOL)
    return "simbolo global indefinido";
  if(status == XV6_TCC_LINK_BAD_SYMBOL)
    return "simbolo ELF invalido";
  if(status == XV6_TCC_LINK_BAD_RELOCATION)
    return "relocacion invalida";
  if(status == XV6_TCC_LINK_RELOCATION_RANGE)
    return "desplazamiento fuera de rango";
  if(status == XV6_TCC_LINK_RELOCATION_ALIGNMENT)
    return "desplazamiento no alineado";
  if(status == XV6_TCC_LINK_RELOCATION_BOUNDS)
    return "relocacion fuera de la seccion combinada";
  if(status == XV6_TCC_LINK_RELOCATION_UNSUPPORTED)
    return "tipo de relocacion no soportado";
  return "error de enlace desconocido";
}
