/*
xv6_tcc_insn.c

Tabla educativa de instrucciones RV64I inspirada en la selección de tokens y
los switch de TinyCC riscv64-asm.c. Cada entrada describe la codificación de
una instrucción real o identifica una pseudoinstrucción que debe expandirse.

Lo que hace este fichero es lo siguiente:
Agrega nuevas funciones para traducir el nombre de la instrucción (ej: add) mediante 
una tabla con los valores hexadecimales. LUego utiliza las funciones anteriores definidas 
en xv6_tcc_asm.c como xv6_tcc_parse_integer para convertir los operandos de la instrucción 
en dígitos (valores numericos y no cadenas), de forma que ya tenemos todos los valores 
de la instrucción en formato numérico y con esa ya puede llamar directamente a las funciones 
para codificar la instrucción completa (tcc_encoded, hay de varios tipos en xv6_tcc_asm.c) y 
así generar la instrucción codificada en 32 bits.

EN resumen, ahora existirá en este fichero una nueva función de condificación "xv6_tcc_encode_named_instruction" que hará todo identificar el formato de la INstrucción
 EJ: I, el código de la operación, los operandos esperados. Luego Va a convertir los elemento sde la instrucción 
 a sus valores númericos respectivo y va a llamar a la función respectiva de codificación según su formato, envíandole
 los argumentos necesarios. EJ a continuación: codificar la siguiente instrucción 

    "addi a0, sp, 16"

 1. xv6_tcc_encode_named_instruction("addi","a0","sp","16",&word); ---> nueva función de este fichero
 2. Esta función va a identificar la instrucción:
   tipo    = formato I
   opcode  = 0x13   ----> con la función xv6_tcc_find_instruction("addi") de este fichero
   funct3  = 0
   operandos esperados = 3
3. Luego va a convertir los operandos a sus valores númericos enteros con las funciones auxiliares en xv6_tcc_asm.c 
   "a0" -> 10
   "sp" -> 2
   "16" -> 16
4. COmo ya tenemos todos los elementos de la instrucción como dígitos y ya sabemos el formato de la instrucción, 
   se procede a llamar a la función de codificación respectiva utilizando los argumentos correctos:
   xv6_tcc_encode_i(0x13,0,10,2,16,&word);
5. COmo resultado se obtiene una codificación binaria de la instrucción inicial addi a0, sp, 16


Basado en:
  riscv64-asm.c https://raw.githubusercontent.com/TinyCC/tinycc/d9d02c56401e43be43760b63f7d82f771a7ed1f6/riscv64-asm.c
  riscv64-tok.h https://raw.githubusercontent.com/Tiny-C-Compiler/tinycc-mirror-repository/d9d02c56401e43be43760b63f7d82f771a7ed1f6/riscv64-tok.h
*/

#include "kernel/types.h" //los tipos de datos de xv6
#include "user/user.h" //strlen, strcmp, memmove...
#include "user/tinycc/xv6_tcc_insn.h" //necesito las funciones de la etapa anterior para codificar instrucciones y para convertir operandos de la instruc a dígitos

#define XV6_TCC_MNEMONIC_MAX 32 /*el nombre de la instrucción se llama mnenomico, es un nombre corto pero se permite que se pongan hasta 31 caracteres (+ nulo)
para que no haya desbordamiento si alguien por accidente pone un nombre muy largo*/


/*Es un array/tabla cuya cada entrada describe cómo es el formato de la operación en este orden:
nombre, tipo, opcode, funct3, funct7, cantidad de operandos

Ej: { "add", XV6_TCC_INSN_R, 0x33, 0x0, 0x00, 3 } significa: add rd, rs1, rs2
    - Formato = R (tres registros)
    - opcode = 0x33 (código de la familia de la operación)
    - funct3 = 0 (es un add, con este valor lo identificamos, si fuera un sub sería 0x20)
    - funct7 = 0
    - operandos = 3 ----> hay operaciones que solo tienen dos operandos como los loads*/
static const struct Xv6TccInstruction instructions[] = {
  { "add",   XV6_TCC_INSN_R,      0x33, 0x0, 0x00, 3 },
  { "sub",   XV6_TCC_INSN_R,      0x33, 0x0, 0x20, 3 },
  { "sll",   XV6_TCC_INSN_R,      0x33, 0x1, 0x00, 3 },
  { "slt",   XV6_TCC_INSN_R,      0x33, 0x2, 0x00, 3 },
  { "sltu",  XV6_TCC_INSN_R,      0x33, 0x3, 0x00, 3 },
  { "xor",   XV6_TCC_INSN_R,      0x33, 0x4, 0x00, 3 },
  { "srl",   XV6_TCC_INSN_R,      0x33, 0x5, 0x00, 3 },
  { "sra",   XV6_TCC_INSN_R,      0x33, 0x5, 0x20, 3 },
  { "or",    XV6_TCC_INSN_R,      0x33, 0x6, 0x00, 3 },
  { "and",   XV6_TCC_INSN_R,      0x33, 0x7, 0x00, 3 },

  { "addi",  XV6_TCC_INSN_I,      0x13, 0x0, 0x00, 3 },
  { "slti",  XV6_TCC_INSN_I,      0x13, 0x2, 0x00, 3 },
  { "sltiu", XV6_TCC_INSN_I,      0x13, 0x3, 0x00, 3 },
  { "xori",  XV6_TCC_INSN_I,      0x13, 0x4, 0x00, 3 },
  { "ori",   XV6_TCC_INSN_I,      0x13, 0x6, 0x00, 3 },
  { "andi",  XV6_TCC_INSN_I,      0x13, 0x7, 0x00, 3 },

  { "lb",    XV6_TCC_INSN_LOAD,   0x03, 0x0, 0x00, 2 },
  { "lh",    XV6_TCC_INSN_LOAD,   0x03, 0x1, 0x00, 2 },
  { "lw",    XV6_TCC_INSN_LOAD,   0x03, 0x2, 0x00, 2 },
  { "ld",    XV6_TCC_INSN_LOAD,   0x03, 0x3, 0x00, 2 },
  { "lbu",   XV6_TCC_INSN_LOAD,   0x03, 0x4, 0x00, 2 },
  { "lhu",   XV6_TCC_INSN_LOAD,   0x03, 0x5, 0x00, 2 },
  { "lwu",   XV6_TCC_INSN_LOAD,   0x03, 0x6, 0x00, 2 },

  { "sb",    XV6_TCC_INSN_STORE,  0x23, 0x0, 0x00, 2 },
  { "sh",    XV6_TCC_INSN_STORE,  0x23, 0x1, 0x00, 2 },
  { "sw",    XV6_TCC_INSN_STORE,  0x23, 0x2, 0x00, 2 },
  { "sd",    XV6_TCC_INSN_STORE,  0x23, 0x3, 0x00, 2 },

  { "beq",   XV6_TCC_INSN_BRANCH, 0x63, 0x0, 0x00, 3 },
  { "bne",   XV6_TCC_INSN_BRANCH, 0x63, 0x1, 0x00, 3 },
  { "blt",   XV6_TCC_INSN_BRANCH, 0x63, 0x4, 0x00, 3 },
  { "bge",   XV6_TCC_INSN_BRANCH, 0x63, 0x5, 0x00, 3 },
  { "bltu",  XV6_TCC_INSN_BRANCH, 0x63, 0x6, 0x00, 3 },
  { "bgeu",  XV6_TCC_INSN_BRANCH, 0x63, 0x7, 0x00, 3 },

  { "lui",   XV6_TCC_INSN_U,      0x37, 0x0, 0x00, 2 },
  { "auipc", XV6_TCC_INSN_U,      0x17, 0x0, 0x00, 2 },
  { "jal",   XV6_TCC_INSN_JAL,    0x6f, 0x0, 0x00, 2 },
  { "jalr",  XV6_TCC_INSN_JALR,   0x67, 0x0, 0x00, 2 },

  /*también se incluyen pseudoinstrucciones: Una pseudoinstrucción se debe transformar en otra instrucción ya que 
  ésta no tiene codificación propia. 
  Ej: mv a0, a1 ----> se transforma en una suma addi a0, a1, 0
  
  Para hacer ese cambio se hace dentro del "switch" de "xv6_tcc_encode_named_instruction" más abajo*/
  { "nop",   XV6_TCC_PSEUDO_NOP,  0, 0, 0, 0 },
  { "mv",    XV6_TCC_PSEUDO_MV,   0, 0, 0, 2 },
  { "not",   XV6_TCC_PSEUDO_NOT,  0, 0, 0, 2 },
  { "neg",   XV6_TCC_PSEUDO_NEG,  0, 0, 0, 2 },
  { "ret",   XV6_TCC_PSEUDO_RET,  0, 0, 0, 0 },
  { "jr",    XV6_TCC_PSEUDO_JR,   0, 0, 0, 1 },
  { "j",     XV6_TCC_PSEUDO_J,    0, 0, 0, 1 },
  { "li",    XV6_TCC_PSEUDO_LI,   0, 0, 0, 2 }
};


/*ELimina espacios del nombre de la instrucción
EL código es básicamente el mismo que usé en "copy_trimmed()" en xv6_tcc_asm.c

Se devuelve el nombre de la instrucción sin espacios en "output"*/
static int
copy_mnemonic(const char *text, char *output, int output_size)
{
  const char *begin;
  const char *end;
  int length;

  if(!text || !output || output_size <= 0)
    return -1;

  begin = text;
  while(*begin == ' ' || *begin == '\t')
    begin++;

  end = begin + strlen(begin);
  while(end > begin && (end[-1] == ' ' || end[-1] == '\t'))
    end--;

  length = end - begin;
  if(length <= 0 || length >= output_size)
    return -1;

  memmove(output, begin, length);
  output[length] = 0;
  return 0;
}

/*Se le manda un texto y comprueba si dentro de ese texto hay un operando (hay un string)
ej : "  sp "
recorre la cadena ssalta espacios y tabuladores hasta que encuentra un caracter.
Esta función no elimina los espacios, sino que verifica que exista el operando. 
Esta función se llamada dentro de "valid_operand_count" para verificar que existan todos los operandos 
de la instrucción a codificar, ya que en instrucciones con 2 operandos al intentar ver si existen 3, uno va a 
ser una cadena vacía con espacio por lo que se devolverá 0*/
static int
operand_present(const char *text)
{
  if(!text)
    return 0; //devuelve 0

  while(*text == ' ' || *text == '\t')
    text++;

  return *text != 0; //devuelve 1
}

/*Está función comprueba el número de operandos que hay en una instrucción, 
utiliza la función auxiliar "valid_operand_count" para ver si el operando existe o es una cadena vacía
y así al final sumar el número de operandos realmente existentes.
Luego compara el número de operandos que existen en la instrucción con los realmente esperados, ya que podría 
ser que la instrucción esté mal formada y falte un operando.

OJO de momento esta función solo cuenta los operando presentes, NO comprueba si esos operandos están en la posición correcta, por 
lo que aunque se mandara algo con los operandos en desorden siempre se sumaría, así que de momento se confía que el que llame 
mande los operandos en orden. CUando integre el parser, éste se encargara de ello  */
static int
valid_operand_count(int expected,
                    const char *operand1,
                    const char *operand2,
                    const char *operand3)
{
  int actual; //contador de operandos

  actual = operand_present(operand1) +
           operand_present(operand2) +
           operand_present(operand3);
  return actual == expected;
}

/*Devuelve la entrada del array de instrucciones correspondiente 
al nombre de la instrucción*/
const struct Xv6TccInstruction *
xv6_tcc_find_instruction(const char *name)
{
  //array donde guardaré el nombre de la instrucción sin espacios
  char mnemonic[XV6_TCC_MNEMONIC_MAX];
  int i;

  //elimino los espacios del nombre de la instrucción y lo guardo en "mnemonic"
  if(copy_mnemonic(name, mnemonic, sizeof(mnemonic)) < 0)
    return 0;

    //recorro el array/tabla de instrucciones
    /*Para no meter el tamaño estático de la tabla hago 
    sizeof(instructions) / sizeof(instructions[0]) 
    en donde sizeof(instructions) = número de elementos x tamaño de cada elemento en bytes
    y sizeof(instructions[0] es el tamaño del elemento en bytes
    entonces (N x tamaño_elemento) / tamaño_elemento = N*/
  for(i = 0; i < (int)(sizeof(instructions) / sizeof(instructions[0])); i++){
    if(strcmp(mnemonic, instructions[i].name) == 0) //comparo si el mnemonic se corresponde con el nombre de esa entrada y si es así, la devuelvo
      return &instructions[i];
  }

  return 0;
}

/*Esta es la función principal para realizar la codificación en 32 bits de una instrucción:
Recibe en cada parámetro los elementos de la instrucción*/
int
xv6_tcc_encode_named_instruction(const char *name,
                                  const char *operand1,
                                  const char *operand2,
                                  const char *operand3,
                                  uint *word)
{

  const struct Xv6TccInstruction *instruction; //variable auxiliar para guardar la entrada de la tabla de instrucciones
  //variables auxiliares en donde guadaré los valores transformados:EJ aquí guardaré los operandos convertidos a dígitos
  int rd; //registro destino
  int rs1; //primer registro fuente o base
  int rs2; //segundo registro fuente
  long immediate; //inmediato

  //valido que la dirección donde se devuelve el resultado (la instrucción codificada en 32 bits) sea válido
  if(!word)
    return -1;

  instruction = xv6_tcc_find_instruction(name); //saco la entrada de la tabla de instrucciones correspondiente al nombre de la instrucción

  //compruebo que la instrucción exista y que la cantidad de operandos recibidos se corresponda con los que espera esa familia de instrucción 
  //identificada en la var anterior
  if(!instruction || !valid_operand_count(instruction->operand_count, operand1, operand2, operand3))
    return -1;


  /*EN este switch decido cómo interpretar los operandos de la instrucción según el tipo de familia o categoría al que pertence la instrucción
  LOs tipos de familia están definidos en el .h de este fichero.
  LO mejor es hacerlo por familia y no por instrucción específica porque así puedo reutilizar código*/
  switch(instruction->kind){

  //instrucciones tipo R (3 registros)
  case XV6_TCC_INSN_R:
    //convierto los 3 registros a dígitos y si algo sale mal, retorno error
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_register(operand2, &rs1) < 0 ||
       xv6_tcc_parse_register(operand3, &rs2) < 0)
      return -1;
    /*luego de tener todos los registros ya como números/dígitos, llamo a la función correspondiente que hará la condificación 
    de la instrucción en un binario de 32 bits: al ser de tipo R pues llamo al encoder de la tipo R*/
    return xv6_tcc_encode_r(instruction->opcode,
                            instruction->funct3,
                            instruction->funct7,
                            rd, rs1, rs2, word);

  //instruccinoes tipo I (2 registros, 1 inmediato)
  case XV6_TCC_INSN_I:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_register(operand2, &rs1) < 0 ||
       xv6_tcc_parse_integer(operand3, &immediate) < 0)
      return -1;
    return xv6_tcc_encode_i(instruction->opcode,
                            instruction->funct3,
                            rd, rs1, immediate, word);

  case XV6_TCC_INSN_LOAD:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_memory_operand(operand2, &immediate, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_i(instruction->opcode,
                            instruction->funct3,
                            rd, rs1, immediate, word);

  case XV6_TCC_INSN_STORE:
    if(xv6_tcc_parse_register(operand1, &rs2) < 0 ||
       xv6_tcc_parse_memory_operand(operand2, &immediate, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_s(instruction->opcode,
                            instruction->funct3,
                            rs1, rs2, immediate, word);

  //de momento en los BRANCH solo acepto números, las etiquetas las agregaré después
  case XV6_TCC_INSN_BRANCH:
    if(xv6_tcc_parse_register(operand1, &rs1) < 0 ||
       xv6_tcc_parse_register(operand2, &rs2) < 0 ||
       xv6_tcc_parse_integer(operand3, &immediate) < 0)
      return -1;
    return xv6_tcc_encode_b(instruction->opcode,
                            instruction->funct3,
                            rs1, rs2, immediate, word);

  case XV6_TCC_INSN_U:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_integer(operand2, &immediate) < 0 ||
       immediate < 0 || immediate > 0xfffffL)
      return -1;
    return xv6_tcc_encode_u(instruction->opcode,
                            rd, (uint)immediate, word);

  case XV6_TCC_INSN_JAL:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_integer(operand2, &immediate) < 0)
      return -1;
    return xv6_tcc_encode_j(instruction->opcode,
                            rd, immediate, word);

  case XV6_TCC_INSN_JALR:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_memory_operand(operand2, &immediate, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_i(instruction->opcode,
                            instruction->funct3,
                            rd, rs1, immediate, word);

  //aquí se transforman todas las pseudoinstrucciones en instrucciones codificables equivalentes

  //nop se expande/transforma en addi x0, x0, 0
  case XV6_TCC_PSEUDO_NOP:
    return xv6_tcc_encode_i(0x13, 0, 0, 0, 0, word);

  //el mv se transforma en un addi (add con inmediato) con 0
  case XV6_TCC_PSEUDO_MV:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_register(operand2, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_i(0x13, 0, rd, rs1, 0, word);

  case XV6_TCC_PSEUDO_NOT:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_register(operand2, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_i(0x13, 4, rd, rs1, -1, word);

  case XV6_TCC_PSEUDO_NEG:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_register(operand2, &rs2) < 0)
      return -1;
    return xv6_tcc_encode_r(0x33, 0, 0x20,
                            rd, 0, rs2, word);

  case XV6_TCC_PSEUDO_RET:
    return xv6_tcc_encode_i(0x67, 0, 0, 1, 0, word);

  case XV6_TCC_PSEUDO_JR:
    if(xv6_tcc_parse_register(operand1, &rs1) < 0)
      return -1;
    return xv6_tcc_encode_i(0x67, 0, 0, rs1, 0, word);

  case XV6_TCC_PSEUDO_J:
    if(xv6_tcc_parse_integer(operand1, &immediate) < 0)
      return -1;
    return xv6_tcc_encode_j(0x6f, 0, immediate, word);

  case XV6_TCC_PSEUDO_LI:
    if(xv6_tcc_parse_register(operand1, &rd) < 0 ||
       xv6_tcc_parse_integer(operand2, &immediate) < 0)
      return -1;
    return xv6_tcc_encode_i(0x13, 0, rd, 0, immediate, word);
  }

  return -1;
}
