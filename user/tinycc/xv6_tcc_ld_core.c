/*
xv6_tcc_ld_core.c

Coordinador del linker modular
Carga objetos ELF64 ET_REL, combina sus secciones, resuelve simbolos, aplica
relocaciones RISC-V y escribe un ejecutable ELF64 ET_EXEC para xv6.
*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_exec_writer.h"

#define XV6_TCC_LD_MAX_INPUTS 8 //máximo número de ficheros objeto de entrada que permito EJ: ldxv6 uno.o dos.o tres.o -o programa
#define XV6_TCC_LD_OBJECT_CAPACITY 32768 //máximo tamaño en bytes de cada fichero objeto. Coincide con el tamaño que puse en la creación del .o en xv6_tcc_as_core.c
#define XV6_TCC_LD_TEXT_CAPACITY 32768
#define XV6_TCC_LD_RODATA_CAPACITY 16384
#define XV6_TCC_LD_DATA_CAPACITY 16384
#define XV6_TCC_LD_EXEC_CAPACITY 131072

static uchar object_data[XV6_TCC_LD_MAX_INPUTS]
                         [XV6_TCC_LD_OBJECT_CAPACITY];
static struct Xv6TccElfBuffer object_storage[XV6_TCC_LD_MAX_INPUTS];
static struct Xv6TccRelObjectView object_views[XV6_TCC_LD_MAX_INPUTS];

static uchar combined_text_data[XV6_TCC_LD_TEXT_CAPACITY];
static uchar combined_rodata_data[XV6_TCC_LD_RODATA_CAPACITY];
static uchar combined_data_data[XV6_TCC_LD_DATA_CAPACITY];
static uchar executable_data[XV6_TCC_LD_EXEC_CAPACITY];
static struct Xv6TccLinkLayout layout;
static struct Xv6TccLinkState link_state;

static void
print_object_summary(int index, const char *path,
                     const struct Xv6TccRelObjectView *view,
                     const struct Xv6TccObjectPlacement *placement)
{
  //Nombre del fichero objeto y su posición en el array de objetos de entrada: Ej: entrada[0]: stage9.o
  fprintf(1, "  entrada[%d]: %s\n", index, path);
  //tamaño del fichero ELF relocatable
  fprintf(1, "    ELF: ET_REL RISC-V, %d bytes\n", view->size);
  fprintf(1, "    .text: %d bytes -> base %d\n",
          (int)view->text_section->sh_size, (int)placement->text_base);
  fprintf(1, "    .rodata: %d bytes -> base %d\n",
          (int)view->rodata_section->sh_size, (int)placement->rodata_base);
  fprintf(1, "    .data: %d bytes -> base %d\n",
          (int)view->data_section->sh_size, (int)placement->data_base);
  fprintf(1, "    .bss: %d bytes -> base %d\n",
          (int)view->bss_section->sh_size, (int)placement->bss_base);
  fprintf(1, "    .symtab: %d entradas\n", view->symbol_count);
  fprintf(1, "    relocaciones: text=%d rodata=%d data=%d\n",
          view->relocation_count,
          view->rodata_relocation_count,
          view->data_relocation_count);
}

static void
print_link_error(int status)
{
  fprintf(2, "ldxv6: %s", xv6_tcc_link_status_text(status));
  if(link_state.error_symbol[0])
    fprintf(2, ": %s", link_state.error_symbol);
  if(link_state.error_object >= 0)
    fprintf(2, " (objeto %d", link_state.error_object);
  if(link_state.error_object >= 0 && link_state.error_relocation >= 0)
    fprintf(2, ", relocacion %d", link_state.error_relocation);
  if(link_state.error_object >= 0)
    fprintf(2, ")");
  fprintf(2, "\n");
}

int
xv6_tcc_ld_core(int input_count, char **inputs, char *output)
{
  struct Xv6TccElfBuffer combined_text;
  struct Xv6TccElfBuffer combined_rodata;
  struct Xv6TccElfBuffer combined_data;
  struct Xv6TccElfBuffer executable;
  int status;
  int i;

  //valido que el número de ficheros objetos entrantes sea mayor a 0 y menos a XV6_TCC_LD_MAX_INPUTS
  if(input_count <= 0 || input_count > XV6_TCC_LD_MAX_INPUTS ||
     !inputs || !output)
    return -1;
  for(i = 0; i < input_count; i++){
    if(!inputs[i] || strcmp(inputs[i], output) == 0){
      fprintf(2, "ldxv6: la salida no puede sustituir una entrada\n");
      return -1;
    }
  }

  combined_text.data = combined_text_data;
  combined_text.size = 0;
  combined_text.capacity = sizeof(combined_text_data);
  combined_rodata.data = combined_rodata_data;
  combined_rodata.size = 0;
  combined_rodata.capacity = sizeof(combined_rodata_data);
  combined_data.data = combined_data_data;
  combined_data.size = 0;
  combined_data.capacity = sizeof(combined_data_data);
  executable.data = executable_data;
  executable.size = 0;
  executable.capacity = sizeof(executable_data);

  if(xv6_tcc_layout_init(&layout, &combined_text,
                         &combined_rodata, &combined_data) < 0){
    fprintf(2, "ldxv6: no se pudo inicializar el layout\n");
    return -1;
  }

  fprintf(1, "ldxv6: enlace ELF64 RISC-V ET_REL -> ET_EXEC\n");

  for(i = 0; i < input_count; i++){
    const struct Xv6TccObjectPlacement *placement;

    /*Se prepara el buffer objeto auxiliar para inspeccionar el fichero objeto actual*/
    //el campo data del buffer objeto apuntará a los bytes en crudo del objeto
    object_storage[i].data = object_data[i];
    //se pone que está vacío porque aún no ha leído nada
    object_storage[i].size = 0;
    //la capacidad de almacenamiento del fichero objeto en crudo será igual al máximo tamaño posible del objeto, ósea XV6_TCC_LD_OBJECT_CAPACITY
    object_storage[i].capacity = sizeof(object_data[i]);

    if(xv6_tcc_load_rel_object(inputs[i], &object_storage[i],
                               &object_views[i]) < 0){
      fprintf(2, "ldxv6: objeto invalido o no legible: %s\n", inputs[i]);
      return -1;
    }
    if(xv6_tcc_layout_add_object(&layout, &object_views[i]) < 0){
      fprintf(2, "ldxv6: no se pudo colocar: %s\n", inputs[i]);
      return -1;
    }

    placement = xv6_tcc_layout_placement_at(&layout, i);
    print_object_summary(i, inputs[i], &object_views[i], placement);
  }

  if(xv6_tcc_layout_finalize(&layout) < 0){
    fprintf(2, "ldxv6: no se pudo finalizar el layout ejecutable\n");
    return -1;
  }

  status = xv6_tcc_link_resolve(&link_state, object_views, input_count,
                                &layout, &combined_text);
  if(status != XV6_TCC_LINK_OK){
    print_link_error(status);
    return -1;
  }

  fprintf(1, "  secciones combinadas:\n");
  fprintf(1, "    .text: %d bytes, alineacion %d\n",
          combined_text.size, layout.text_align);
  fprintf(1, "    .rodata: %d bytes, alineacion %d\n",
          combined_rodata.size, layout.rodata_align);
  fprintf(1, "    .data: %d bytes, alineacion %d\n",
          combined_data.size, layout.data_align);
  fprintf(1, "    .bss: %d bytes, alineacion %d\n",
          (int)layout.bss_size, layout.bss_align);
  fprintf(1, "  simbolos globales resueltos: %d\n",
          link_state.global_count);
  fprintf(1, "  relocaciones aplicadas: %d\n",
          link_state.applied_relocations);
  if(xv6_tcc_write_exec_file(&layout, &link_state,
                              &combined_text, &combined_rodata,
                              &combined_data, output, &executable) < 0){
    fprintf(2, "ldxv6: no se pudo escribir ET_EXEC: %s\n", output);
    return -1;
  }

  fprintf(1, "  direcciones finales:\n");
  fprintf(1, "    .text: %d\n", (int)layout.text_address);
  fprintf(1, "    .rodata: %d\n", (int)layout.rodata_address);
  fprintf(1, "    .data: %d\n", (int)layout.data_address);
  fprintf(1, "    .bss: %d\n", (int)layout.bss_address);
  fprintf(1, "  salida ET_EXEC: %s (%d bytes)\n",
          output, executable.size);
  fprintf(1, "   ejecutable generado correctamente\n");
  return 0;
}
