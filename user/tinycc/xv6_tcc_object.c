/*
xv6_tcc_object.c

Es una reprentación del fichero objeto a generar pero en memoria, todavía no genera el ELF 
en el disco. 
Construccion educativa de simbolos y relocaciones.
La organizacion sigue el modelo de TinyCC tccelf.c y riscv64-asm.c, pero
sustituye sus estados dinamicos y callbacks por arrays de capacidad fija
adecuados para xv6.
Este fichero recibirá las líneas de código ya separadas por el parser de líneas 
y con ellas va construyendo las estructuras del ELF en memoria, todavía no se 
escriben en almacenamiento persistente.
En resumen es un estado educativo de un objeto ELF relocatable mientras se ensambla.
Mantiene el codigo de .text, los simbolos descubiertos y las relocaciones
pendientes. De momento todo se construye en memoria, luego me encargaré 
de escribir el fichero ELF64 ET_REL completo.

Donante conceptual:
  TinyCC tccelf.c y riscv64-asm.c

*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_object.h"

/*Función auxiliar que busca un símbolo por su nombre en el array temporal 
de símbolos dentro de la estructura temporal de construcción del fichero objeto.
Se devuelve la posición interna dentro del array temporal */
static int
symbol_slot_by_name(const struct Xv6TccObjectBuilder *object,
                    const char *name)
{
  int i;

  //válido que exista el objeto y el nombre del símbolo
  if(!object || !name)
    return -1;

  //recorro los símbolos
  for(i = 0; i < object->symbol_count; i++)
    if(strcmp(object->symbols[i].name, name) == 0)
      return i; //deevuelvo la posición interna

  return -1;
}

/*Crea un nuevo símbolo  en el array temporal 
de símbolos dentro de la estructura temporal de construcción del fichero objeto.
Esta función no define el símbolo (definir su refencia) porque no todos los símbolos podrían estar definidos en este fichero objeto
sino que podrían estar definidos en otro fichero*/
static int
create_symbol(struct Xv6TccObjectBuilder *object,
              const char *name, int *slot)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int length;

  //hago validaciones
  if(!object || !name || !slot || !xv6_tcc_valid_identifier(name))
    return -1;
  //como límite he puesto que el máximo número de símbolos será 32
  if(object->symbol_count >= XV6_TCC_OBJECT_MAX_SYMBOLS)
    return -1;

  //Verifico que el símbolo a meter no tenga un nombre mayor ya XV6_TCC_LINE_NAME_MAX
  length = strlen(name);
  if(length <= 0 || length >= (int)sizeof(object->symbols[0].name))
    return -1;

  //busco el siguiente slot libre en el array de símbolos auxiliar para meter el nuevo
  *slot = object->symbol_count;
  //obtengo al dirección en el array de símbolos para meter el nuevo valor
  symbol = &object->symbols[object->symbol_count];
  //aumento el contador
  object->symbol_count++;
  /*POngo todos los campos el símbolo a cero:
  value = 0
  size = 0
  defined = 0
  elf_index = 0
  ...*/
  memset(symbol, 0, sizeof(*symbol));
  //copio el nombre del nuevo símbolo con el nulo
  memmove(symbol->name, name, length + 1);
  //pongo el estado inicial del símbolo: símbolo local y sin definir su referencia, ya que no se sabe si aparecerá .global o si se definirá en este fichero
  symbol->binding = XV6_TCC_STB_LOCAL;
  symbol->section_index = XV6_TCC_SHN_UNDEF;
  return 0;
}

/*Busca un símbolo y si no existe lo crea.

Esto sirve para cuando tengamos símbolos con referencias adelantadas, es decir, aparece una etiqueta para un salto pero todavía no sabemos 
a dónde va hasta que aparezca con los ":"
Ej:
beq a0, zero, done
...
done:

y devuelvo el slot que ocupa en el array temporal de símbolos*/
static int
get_or_create_symbol(struct Xv6TccObjectBuilder *object,
                     const char *name, int *slot)
{
  int found;

  //si existe el símbolo, devuelvo el slot que ocupa en el array temporal de símbolos
  found = symbol_slot_by_name(object, name);
  if(found >= 0){
    *slot = found;
    return 0;
  }

  //si no existe, creo el símbolo
  return create_symbol(object, name, slot);
}

/*Crea u obtiene el símbolo (llamando a get_or_create_symbol) pero además lo define (la definición es con la etiqueta con los ":")
agregando su posición concreta dentro de .text
Esto ocurre cuando se encuentra una referencia adelantada:
beq a0, zero, done
done:
como se encuentra "done" antes que "done:", al hacer create_symbol() no se sabe dónde 
está definido el símbolo, por eso cuando aparece realmente la definición "done:", se debe 
llamar a esta función define_label(object, "done");*/
static int
define_label(struct Xv6TccObjectBuilder *object, const char *name)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int slot;

  //obtiene o crear el símbolo en el array de símbolos temporal y devuelve su posición en el array
  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;

  /*se obtiene el símbolo del array. SI el símbolo ya está definido es un error, ósea se ha detectado 
  una definición duplicada. EJ:
  loop:
    addi a0, a0, 1

  loop:
      ret
      
  COmo hay definición duplicada, se rechaza*/
  symbol = &object->symbols[slot];
  if(symbol->defined)
    return -1;

  //Se define el símbolo
  symbol->defined = 1;
  symbol->section_index = XV6_TCC_SHN_TEXT;
  /*La posición de la etiqueta es la canditad de bytes que ya había en .text hasta encontrar 
  la definición del símbolo actual, ej: 
  addi a0, zero, 1  # 4 bytes
  beq a0, zero, done # 4 bytes
  j external_func    # 4 bytes
  done:
  
  Como ya habíamos procesado todas las líneas antes de "done:" vemos que todas ocupaban 12 bytes, así 
  que "done:" será 12*/
  symbol->value = object->text->size;
  symbol->size = 0;
  return 0;
}

/*Marca un símbolo como visible globalmente
Esto se usa para: 
.globl main
main:

Como main aparece antes, se pone por defecto como local*/
static int
mark_global(struct Xv6TccObjectBuilder *object, const char *name)
{
  int slot;

  if(get_or_create_symbol(object, name, &slot) < 0)
    return -1;

  object->symbols[slot].binding = XV6_TCC_STB_GLOBAL;
  return 0;
}

/*Esta función se utiliza cuando una instrucción no tiene un 
desplazamiento numéricos, sino un símbolo como desplazamiento.
Ej:
- beq a0, zero, done
- j loop
Para generar el objeto, esos desplazamientos se sustityen por 0 para que luego el linker 
ponga los valores correctos. Pero además creará la relocación pendiente*/
static int
emit_symbolic_instruction(struct Xv6TccObjectBuilder *object,
                          const struct Xv6TccParsedLine *line,
                          const struct Xv6TccInstruction *instruction)
{
  const char *symbol_name;
  const char *operand1;
  const char *operand2;
  const char *operand3;
  uint relocation_type;
  uint word;
  uint offset;

  //Primero recupero los operandos de la instrucción y los meto en variables auxiliares
  //si la instrucción no tiene los 3 operandos por su formato, se pone 0
  operand1 = line->operand_count > 0 ? line->operands[0] : 0;
  operand2 = line->operand_count > 1 ? line->operands[1] : 0;
  operand3 = line->operand_count > 2 ? line->operands[2] : 0;
  //todavía no hemos determinado el nombre del símbolo ni el tipo de relocación
  symbol_name = 0;
  relocation_type = 0;

  /*Verifico si la instrucción es un branch simbólico, estos branches tienen el símbolo del desplazamiento en el tercer operando
  Ej: beq rs1, rs2, destino ---> beq a0, zero, done*/
  if(instruction->kind == XV6_TCC_INSN_BRANCH &&
     operand3 && xv6_tcc_valid_identifier(operand3)){
    //guardo el nombre del símbolo
    symbol_name = operand3;
    //provisionalmente se codificará el desplazamiento de ese símbolo a 0 Ej: beq a0, zero, 0
    operand3 = "0";
    //y por último le indico al linker que deberá resolver esto aplicando una reloación branch "relocation_type = XV6_TCC_R_RISCV_BRANCH"
    relocation_type = XV6_TCC_R_RISCV_BRANCH;
  } 
    /*Verifico si la instrucción es un JAL símbolico. En esta instrucción el símbolo está en el segundo operando.
    EJ: jal rd, destino*/
    else if(instruction->kind == XV6_TCC_INSN_JAL &&
            operand2 && xv6_tcc_valid_identifier(operand2)){
            //guardo el nombre del símbolo
            symbol_name = operand2;
            //provisionalmente se codificará el desplazamiento de ese símbolo a 0 Ej: jal ra, 0
            operand2 = "0";
            //y por último le indico al linker que deberá resolver esto aplicando una reloación branch "relocation_type = XV6_TCC_R_RISCV_JAL"
            relocation_type = XV6_TCC_R_RISCV_JAL;
            } 
              /*Verifico si la instrucción es la pseudoinstrucción j. Esta instrucción tiene el símbolo en el primer operando.
              EJ: j external_func. */
              else if(instruction->kind == XV6_TCC_PSEUDO_J &&
                      operand1 && xv6_tcc_valid_identifier(operand1)){
              symbol_name = operand1;
              //provisionalmente se codificará el desplazamiento de ese símbolo a 0 
              operand1 = "0";
              //y por último le indico al linker que deberá resolver esto aplicando una reloación branch "relocation_type = XV6_TCC_R_RISCV_JAL"
              relocation_type = XV6_TCC_R_RISCV_JAL;
                    } else {
                      return -1;
                    }
              //de momento solo permito branches y saltos con símbolos, luego permitiré operaciones con símbolos EJ: addi a0, a0, %lo(simbolo)

  //pongo las llaves para crear un ámbito local
  {
    struct Xv6TccAssemblerRelocation *relocation;
    int symbol_slot;

    //verifico que todavía me quepan relocaciones en el fichero objeto y que el símbolo exista, sino se crea y me devuelve el slot donde está 
    if(object->relocation_count >= XV6_TCC_OBJECT_MAX_RELOCATIONS ||
       get_or_create_symbol(object, symbol_name, &symbol_slot) < 0)
      return -1;

    //códifico la instrucción provisional con el símbolo sustituido por cero
    if(xv6_tcc_encode_named_instruction(line->name,
                                         operand1, operand2, operand3,
                                         &word) < 0)
      return -1;

    //se guarda la instrucción codificada en la sección .text y me devuelve el offset en dónde empieza esa nueva instrucción insertada
    if(xv6_tcc_emit32(object->text, word, &offset) < 0)
      return -1;

    //reservo espacio para la relocación de ese símbolo y luego aumento el contador de relocaciones. EL "relocation_count++" va despuès de calcular 
    //relocation = &object->relocations[object->relocation_count], ósea se incrementa después. Esto se podría sustituir por 2 operaciones separadas
    relocation = &object->relocations[object->relocation_count++];
    //guardo el offset de esa instrucción en el .text sacado arriba
    relocation->offset = offset;
    //pongo el tipo de relocación
    relocation->type = relocation_type;
    //meto su slot en el array temporal de símbolos
    relocation->symbol_slot = symbol_slot;
    relocation->addend = 0;
  }

  return 0;
}

/*INtenta codificar una instrucción de formal normal (sin símbolos), si funciona termina.
SI falla, comprueba si hay una referencia simbólica (ya que el símbolo al no ser un número 
fallaría en la comprobación de xv6_tcc_parse_integer("done", &immediate)).
Si es una instrucción con una referencia simbólica, se emite la instrucción provisional + relocación*/
static int
emit_instruction(struct Xv6TccObjectBuilder *object,
                 const struct Xv6TccParsedLine *line)
{
  const struct Xv6TccInstruction *instruction;
  uint offset;

  if(xv6_tcc_emit_parsed_instruction(line, object->text, &offset) == 0)
    return 0;

  instruction = xv6_tcc_find_instruction(line->name);
  if(!instruction)
    return -1;

  return emit_symbolic_instruction(object, line, instruction);
}

/*Procesa las directivas en la sección ".text" pero no hace nada de momento. 
COmo las secciones .data y .rodata aún no están implementadas se rechazan. 
Además de momento se rechazan directivas como ".word 42", ".asciz "HOla". EL parser de líneas si las reconoce pero todavía no el constructor de objetos*/
static int
process_directive(struct Xv6TccObjectBuilder *object,
                  const struct Xv6TccParsedLine *line)
{
  int i;

  if(strcmp(line->name, ".text") == 0)
    return 0;

  if(strcmp(line->name, ".section") == 0){
    if(line->operand_count >= 1 &&
       strcmp(line->operands[0], ".text") == 0)
      return 0;
    return -1;
  }

  //.globl y .global significan lo mismo. Aquí se marcan como globales todos los símbolos en la misma línea de la directiva .global EJ: .globl main, funcion
  //tanto "main" como "funcion" se marcan como globales
  if(strcmp(line->name, ".globl") == 0 ||
     strcmp(line->name, ".global") == 0){
    for(i = 0; i < line->operand_count; i++)
      if(mark_global(object, line->operands[i]) < 0)
        return -1;
    return 0;
  }

  //directivas ignoradas, se aceptan pero todvaía no las aplico
  if(strcmp(line->name, ".option") == 0 ||
     strcmp(line->name, ".file") == 0 ||
     strcmp(line->name, ".ident") == 0 ||
     strcmp(line->name, ".attribute") == 0 ||
     strcmp(line->name, ".type") == 0 ||
     strcmp(line->name, ".size") == 0)
    return 0;

  return -1;
}

/*Va a convertir un símbolo interno del array temporal de símbolos en una entrada ELF real de la tabla .symtab*/
static int
append_final_symbol(struct Xv6TccObjectBuilder *object, int slot)
{
  struct Xv6TccAssemblerSymbol *symbol;
  int shndx;
  int info;
  uint index;

  //se obtiene el símbolo
  symbol = &object->symbols[slot];
  //se determinada si el símbolo está definido y ne qué sección o si el símbolo no está definido en este fichero objeto
  if(symbol->defined)
    shndx = symbol->section_index;
  else
    shndx = XV6_TCC_SHN_UNDEF;

  //construye el campo st_info del símbolo que combina: el binding (local o global) y el tipo del símbolo (aunque de momento todos etndrán de tipo "notype")
  info = xv6_tcc_elf_st_info(symbol->binding, XV6_TCC_STT_NOTYPE);

  //inserta el símbolo en .strtab y añade la entrada ELF64_Sym a .symtab, y nos devuelve su índice
  if(xv6_tcc_put_elf_sym(object->symtab, object->strtab,
                         symbol->value, symbol->size,
                         info, 0, shndx,
                         symbol->name, &index) < 0)
    return -1;

  //el índice devuelto al haberlo intersetado en las tablas será usado para las relocaciones finales
  symbol->elf_index = index;
  return 0;
}

/*Esta función sirve para inicializar todos los buferes para crear los elementos del ELF en memoria*/
int
xv6_tcc_object_init(struct Xv6TccObjectBuilder *object,
                    struct Xv6TccElfBuffer *text,
                    struct Xv6TccElfBuffer *symtab,
                    struct Xv6TccElfStringTable *strtab,
                    struct Xv6TccElfBuffer *rela_text)
{
  //valido que existan los búferes y que además existan los búferes internos de ellos "data" donde se guard la info
  if(!object || !text || !symtab || !strtab || !rela_text ||
     !text->data || !symtab->data || !strtab->data || !rela_text->data)
    return -1;

  //se inicializan el estado del fichero objeto en memoria a 0 todos sus campos 
  memset(object, 0, sizeof(*object));
  //se guardan los punteros de los búfferes en el fichero objeto 
  object->text = text;
  object->symtab = symtab;
  object->strtab = strtab;
  object->rela_text = rela_text;

  //se indica que los búferes están libres (contienen 0 bytes)
  text->size = 0;
  symtab->size = 0;
  strtab->size = 0;
  rela_text->size = 0;
  return 0;
}

/*Procesa una línea ya analizada por el analizador de líneas xv6_tcc_parse_line()
para meterla al fichero objeto. También procesa directivas, líneas vacías y líneas con etiquetas, no solo líneas con instrucciones*/
int
xv6_tcc_object_process_line(struct Xv6TccObjectBuilder *object,
                            const struct Xv6TccParsedLine *line)
{
  //se valida que exista el objeto y la línea. No se pueden agregar más líneas si el fichero objeto se ha finalizdo
  if(!object || !line || object->finalized)
    return -1;

  //si la línea tiene una etiqueta, hay que definirla
  if(line->has_label && define_label(object, line->label) < 0)
    return -1;

  //si la línea está contiene solo una etiqueta "ej: loop:", pues la etiqueta fue procesada en el paso anterior así qeu no se hace nada
  if(line->kind == XV6_TCC_LINE_EMPTY)
    return 0;

  //si es una directiva la procesa
  if(line->kind == XV6_TCC_LINE_DIRECTIVE)
    return process_directive(object, line);

  //si la línea es una instrución, se procesa
  if(line->kind == XV6_TCC_LINE_INSTRUCTION)
    return emit_instruction(object, line);

  //línea desconocida
  return -1;
}

/*Esta función convierte el estado temporal del ELF con los arrays auxiliares para símbolos y relocaciones (symbols[], relocations[]) en las 
tablas ELF definitivas ".strtab, .symtab, .rela.text"*/
int
xv6_tcc_object_finalize(struct Xv6TccObjectBuilder *object)
{
  struct Xv6TccAssemblerRelocation *relocation;
  struct Xv6TccAssemblerSymbol *symbol;
  uint empty_name_offset;
  uint null_symbol_index;
  uint64 info;
  int i;

  //no se puede finalizar un fichero objeto 2 veces
  if(!object || object->finalized)
    return -1;

  //se vacían las tablas finales del fichero objeto ya que se van a construir desde cero utilizando los arrays auxiliares
  //no vaciamos .text porque el código generado debe conservarse
  object->symtab->size = 0;
  object->strtab->size = 0;
  object->rela_text->size = 0;

  //se agrega la primera cadena a .strtab la cual debe ser una cadena vacía y va en el offset 0, si no está en ese offset ocurre un error
  if(xv6_tcc_put_elf_str(object->strtab, "", &empty_name_offset) < 0 || empty_name_offset != 0)
    return -1;

  //la entrada cero de .symtab también debe ser el símbolo nulo con todos sus campos a cero. Los símbolos sreales empiezan en el índice 1
  if(xv6_tcc_put_elf_sym_raw(object->symtab, 0, 0, 0,
                             0, 0, XV6_TCC_SHN_UNDEF,
                             &null_symbol_index) < 0 ||
     null_symbol_index != 0)
    return -1;

  /*Se recorre toda la tabla de símbolos auxiliar para ver si están definidos (referenciados). Si no está definido, probablemente es porque es un 
  símbolo definido en otro fichero objeto. Entonces el símbolo indefinido se marcada como global de forma que:
  símbolo ---> undefined, global ---> y con esta información el linker va a saber que se necesita un símbolo con ese nombre definido en otro lugar*/
  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(!symbol->defined)
      symbol->binding = XV6_TCC_STB_GLOBAL;
  }

  /*Se agregan los símbolos locales a la tabla ".symtab".
  ELF exige colocar primero los símbolos locales*/
  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(symbol->binding == XV6_TCC_STB_LOCAL &&
       append_final_symbol(object, i) < 0)
      return -1;
  }

  /*Cálculo la posición donde comenzará el primer símbolo global. Como los símbolos globales van después que los locale y ya hemos metidos todos los locales
  simplemente sacamos el tamaño en bytes de todos los símbolos locales (cada entrada es un struct Xv6TccElfSym que al momento de hacer  de hacer esto ocupa 24 bytes)
  y así saco el comienzo donde meteré los símbolos globales*/
  object->first_global_symbol = object->symtab->size / sizeof(struct Xv6TccElfSym);

  //meto los símbolos globales a la tabla .symtab 
  for(i = 0; i < object->symbol_count; i++){
    symbol = &object->symbols[i];
    if(symbol->binding == XV6_TCC_STB_GLOBAL &&
       append_final_symbol(object, i) < 0)
      return -1;
  }

  /*En este punto tendría en .symtab los siguiente por ejemplo:
  índice 0 → nulo
  índice 1 → done, local
  índice 2 → main, global
  índice 3 → external_func, global indefinido*/

  //recorro el array de relocaciones pendientes para meter en el buffer de reloaciones todas las relocaciones acumuladas en el array auxiliar
  for(i = 0; i < object->relocation_count; i++){
    //saco la relocación
    relocation = &object->relocations[i];
    //obtengo el símbolo asociado a esa relocación
    symbol = &object->symbols[relocation->symbol_slot];
    //creo el campo r_info de esa relocación
    info = xv6_tcc_elf_r_info(symbol->elf_index, relocation->type);

    //con toda esa info anterior ya puedo crear la relocación real Elf64_Rela en el buffer de relocacines 
    if(xv6_tcc_put_elf_rela(object->rela_text,
                            relocation->offset,
                            info,
                            relocation->addend) < 0)
      return -1;
  }

  //marco el fichero objeto como finalizado
  object->finalized = 1;
  return 0;
}

/*Función auxiliar para buscar un símbolo dado un nombre*/
const struct Xv6TccAssemblerSymbol *
xv6_tcc_object_find_symbol(const struct Xv6TccObjectBuilder *object,
                           const char *name)
{
  int slot;

  slot = symbol_slot_by_name(object, name);
  if(slot < 0)
    return 0;
  return &object->symbols[slot];
}

/*Función auxiliar para buscar un símbolo dado su posición o índice en el array de símbolos*/
const struct Xv6TccAssemblerRelocation *
xv6_tcc_object_relocation_at(const struct Xv6TccObjectBuilder *object,
                             int index)
{
  if(!object || index < 0 || index >= object->relocation_count)
    return 0;
  return &object->relocations[index];
}
