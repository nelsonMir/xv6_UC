/*
xv6_tcc_line.c

Analizador reducido de líneas inspirado conceptualmente en la separación de
líneas, comentarios, operandos, etiquetas y directivas de TinyCC. Esta etapa
solo reconoce la estructura. La creación de símbolos y relocaciones todavía no.

Donante conceptual:
  TinyCC tccasm.c, riscv64-asm.c y el parser reducido del paquete de referencia
 
Este fichero va a analizar una línea de código y la va a analizar por completo: 
Identificar todos sus elementos individualmente y así poder llamar a la función de codificación 
de la instrucción respectiva con los argumentos correctos.
*/

#include "kernel/types.h"
#include "user/user.h"
#include "user/tinycc/xv6_tcc_line.h"

/*Esta estructura se utiliza para describir el máximo y mínimo número de operando que permite una directiva

Ej: { ".text", 0, 0 }
ENtonces una línea podría ser ".text" pero una línea errónea sería ".text add sp" ya que no permite argumentos, ósea más elementos en la misma línea

Ej 2: { ".equ", 2, 2 }
EN este caso sería correcto ".equ TAMANO, 64"

Esta estructura se usará luego para hacer una matriz que describe todas las directivas, para indicar si permite o no operandos
esto se hace a través del campo mínimos operandos y máximos operandos permitidos*/
struct Xv6TccDirectiveDescription {
  const char *name;
  int minimum_operands;
  int maximum_operands;
};

/*Matriz/tabla que indica el número de operandos que permite una directiva en la misma línea*/
static const struct Xv6TccDirectiveDescription directives[] = {
  { ".text",      0, 0 }, //la directiva .text no acepta operandos ya que el mínimo y máximo es 0
  { ".rodata",    0, 0 },
  { ".data",      0, 0 },
  { ".bss",       0, 0 },
  { ".section",   1, 3 },
  { ".globl",     1, 3 },
  { ".global",    1, 3 },
  { ".weak",      1, 3 },
  { ".local",     1, 3 },
  { ".equ",       2, 2 },
  { ".set",       2, 2 },
  { ".align",     1, 3 },
  { ".p2align",   1, 3 },
  { ".balign",    1, 3 },
  { ".byte",      1, 3 },
  { ".2byte",     1, 3 },
  { ".half",      1, 3 },
  { ".4byte",     1, 3 },
  { ".word",      1, 3 },
  { ".8byte",     1, 3 },
  { ".dword",     1, 3 },
  { ".ascii",     1, 3 },
  { ".asciz",     1, 3 },
  { ".string",    1, 3 },
  { ".zero",      1, 2 },
  { ".space",     1, 2 },
  { ".skip",      1, 2 },
  { ".option",    1, 3 },
  { ".file",      1, 3 },
  { ".ident",     1, 3 },
  { ".attribute", 1, 3 },
  { ".type",      2, 2 },
  { ".size",      2, 2 },
  { ".hidden",    1, 3 },
  { ".protected", 1, 3 },
  { ".internal",  1, 3 }
};

/*FUnción auxiliar que salta espacios, tabuladores, saltos de línea y retornos de carro.
SOlo salta ese carácter una vez, pero se utilizará en este fichero en otras funciones en loops para saltarse esos espacios 
hasta llegar a caracteres útiles de la línea.
REtorna el espacio, el tabulador, el salto de línea o el retorno de carro*/
static int
space_character(int character)
{
  return character == ' ' || character == '\t' ||
         character == '\r' || character == '\n';
}

/*FUnción auxiliar que avanza el puntero de una línea hasta encontrar un caracter útil (que no sea un espacio), para 
ello utilizo la función auxiliar de antes. 
OJo aquí no me pongo a mover ni copiar la cadena, solo muevo un puntero */
static char *
skip_spaces(char *text)
{
  //salto caracteres hasta encontrar el primer útil o hasta que no se haya llegado al final de la línea "\0"
  while(*text && space_character(*text))
    text++;
  return text;
}

/*ELimino caracteres a la derecha que sean espacios para ello modificando la cadena*/
static void
trim_right(char *text)
{
  int length;

  length = strlen(text);
  while(length > 0 && space_character(text[length - 1])){
    text[length - 1] = 0;
    length--;
  }
}

/*ELimina espacios tanto a la derecha como a la izquierda (ambos lados)*/
static char *
trim_text(char *text)
{
  char *begin;

  //primero salta todos los espacios a la izzquierda y en begin recibe el puntero apuntado al primer 
  //caracter válido
  begin = skip_spaces(text);
  trim_right(begin); //elimina los espacios a la derecha
  return begin; //devuelve la cadena sin espacios y con el puntero al inicio útil
}

/*Copia la línea entera a un buffer que pueda ser modificable (se manda la línea y la longitud del buffer) y lo devuelve en "output"
porque el argumento en el que recibo la línea es un const*/
static int
copy_source_line(const char *text, char *output, int output_size)
{
  int length;

  //Rechaza punteros nulos o un tamaño inválido
  if(!text || !output || output_size <= 0)
    return -1;

   //saco la longitud de la línea
  length = strlen(text);
  //si la línea es mayor al buffer, error
  if(length >= output_size)
    return -1;

  //se copia la línea 
  memmove(output, text, length + 1);
  return 0;
}

//elimina comentarios en la línea que comiencen por "#". NO eliminar "#" que se encuentren dentro de una cadena ej: .asciz "Hola # mundo"
static void
remove_comment(char *text)
{
  /*Para no eliminar "#" dentro de una cadena uso "quoted" = 1 significa que se está dentro de comillas*/
  int quoted;
  /*"escaped"=1 indica que el caracter anterior era una \ dentro de una cadena (dentro de comillas), ósea sirve para "escapar" el carácter actual EJ: \" eso significa que la comilla no se interpetra como 
  comilla si no como texto */
  int escaped;

  //se inicializan
  quoted = 0;
  escaped = 0;

  //se va a procesar toda la línea hasta llegar al caracter nulo "\0"
  while(*text){

    //si el carácter actual está escapado entonces no tiene significado especial
    if(escaped){
      escaped = 0;
    //si estamos dentro de una cadena (dentro de comillas) y se cuenta una \ significa que el siguiente caracter se debe marcar como escapado.
    //se pone en la condición "\\" porque debo escapar el \ para interpretarlo como una \.
    } else if(quoted && *text == '\\'){
      escaped = 1;
        //si se encuentra una comilla, si es la primera vez significa que se abre la cadena, si es la segunda vez se cierra la cadena.
        //uso el operador "!" en !quoted para intercambiar el valor y así saber si se abre o se cierra la cadena
        } else if(*text == '"'){
        quoted = !quoted;
            //si se encuentra un "#" fuera de una cadena (fuera de comillas "") es un comentario, así que lo reemplazo por el caracter nulo "\0"
            //ej: addi a0, a1, 1 # comentario  ----------> addi a0, a1, 1 \0 comentario, por lo que la línea terminaría básicamente al salir \0 y "comentario" ya no se interpretaría
            } else if(*text == '#' && !quoted){
            *text = 0;
            return;
        }
    text++;
  }
}

/*Función auxilar llamada en valid_identifier para verificar que el carácter inicial de una etiqueta sea correcto (ahora esta función tb se utiliza para validad símbolos/nombres simbólicos,
las directivas por otro lado se validan con "find_directive()").
UNa etiqueta puede comenzar por:
- minusculas: a-z
- maýusculas: A-Z
- guión bajo: _
- punto: .
- dólar: $

NO puede comenzar por un número, ej: 5main*/
static int
identifier_start(int character)
{
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         character == '_' || character == '.' || character == '$';
}

/*Después de validar el primer ccaracter, se permite que los siguientes sean dígitos
Esta función auxiliar se utiliza en valid_identifier() para recorrer todos los caracteres después de validar el primero 
para ver si son válidos, esta vez se permiten:
- numeros: 0-9
- minusculas: a-z
- maýusculas: A-Z
- guión bajo: _
- punto: .
- dólar: $
EJ:
loop1
.L42
funcion2*/
static int
identifier_character(int character)
{
  return identifier_start(character) ||
         (character >= '0' && character <= '9');
}

/*Función utililzada para comprobar que una etiqueta tenga un identificador válido*/
int
xv6_tcc_valid_identifier(const char *text)
{
  int i;

  /*Se comprueba que la cadena no esté vacía, no sea NULL y que no empiece por un número*/
  if(!text || !identifier_start(text[0]))
    return 0;

  //Se recorren el resto de caracteres para ver que sean válidos
  for(i = 1; text[i] != 0; i++)
    if(!identifier_character(text[i]))
      return 0;

  return 1;
}

/*Recorre una línea desde el comienzo hasta encontrar dos puntos ":", que signica que la cadena anterior a estos
es una etiqueta.
Devuelve el puntero a la línea justo apuntando a ":"
EJ: loop: add a0, a1, a2
IMP si en dado caso hay un espacio entonces se decarta, es decir, "loop  :" no es válido, el ":" desde estar pegado al texto.
Si encuentran los ":" válidos, se retornan estos*/
static char *
find_label_colon(char *text)
{
  //recorre la línea hasta llegar al caracter nulo
  while(*text){
    if(*text == ':')
      return text;
    //si en dado caso hay un espacio entonces se decarta, es decir, "loop :" no es válido, el ":" desde estar pegado al texto
    if(space_character(*text))
      return 0;

    text++;
  }

  return 0;
}

/*COpia el nombre de un elemento en un buffer de destinno*/
static int
copy_name(const char *source, char *destination, int destination_size)
{
  int length;

  length = strlen(source);
  //se rechazan nombres vacíos o demasiado largos
  if(length <= 0 || length >= destination_size)
    return -1;

  //realiza la copia
  memmove(destination, source, length + 1);
  return 0;
}

/*Extrae la primer palabra de una línea (genera el primer token), después de quitar una posible etique "ej loop:"
EJ: "addi a0, a1, 42"
devuelve en "name" = addi, y devuelve el resto de la cadena en el retun final = "a0, a1, 42"*/
static char *
first_token(char *text, char *name, int name_size)
{
  char *cursor; //puntero auxiliar a la línea
  int length;

  //se saltan espacios hasta llegar a la primera palabra de la línea
  cursor = skip_spaces(text);
  length = 0;

  //se copia copia de caracter en caracter de la primera palabra hasta llegar al primer espacio, es decir: 
  //inicialmente: addi a0, a1, 42 ---> name = addi
  while(cursor[length] && !space_character(cursor[length])){
    if(length >= name_size - 1)
      return 0;
    name[length] = cursor[length];
    length++;
  }

  if(length == 0)
    return 0;

  name[length] = 0;
  //devuelve el resto de la cadena quitando la primera palabra
  return skip_spaces(cursor + length);
}

/*Después de quitar la primera palabra de la instrucción, va a separar el texto restante por comas, pero solo cuando la coma está 
fuera de cadenas "" y paréntesis

EJ: a1, 16(sp) 
va a devolver el array "operands" así:
operands[0] = "a1"
operands[1] = "16(sp)"

Ej: si el texto está entre comillas y hay comas no hay que dividir nada:
"Hola, mundo" ---> no se divide*/
static int
split_operands(char *text,
               char operands[XV6_TCC_LINE_MAX_OPERANDS]
                            [XV6_TCC_LINE_OPERAND_MAX])
{
  char *start; //Apunta al inicio del operando actual
  char *cursor; //Recorre la cadena carácter a carácter
  int count;//Número de operandos encontrados
  int parenthesis_depth; //Cuenta la profundidad de paréntesis: 0 --> fuera de paréntesi, 1 -->dentro de un par, 2 -->paréntesis anidados
  int quoted; //"quoted" = 1 significa que se está dentro de comillas
  int escaped; //Controla caracteres escapados dentro de cadenas

  //pone el puntero al inicio del primer operando quitando espacios anteriores
  start = skip_spaces(text);
  //si no hay caracteres (ósea todos son espacios), entonces no hay operandos 
  if(*start == 0)
    return 0;

  //estado inicial antes de detectar operandos
  cursor = start; //el cursor se pone al inicio de la cadena de operandos
  count = 0;
  parenthesis_depth = 0;
  quoted = 0;
  escaped = 0;

  /*este bluce procesa toda la línea de operandos hasta llegar al nulo del final, cuando se llega a ese nulo se sale con un break, para 
  ello el proceso es el siguiente:
   
   
  Luego, el puntero start (que apuntaba al inicio de la cadena de operandos) se eliminan espacios para que apunte al primer caracter 
  del operando y luego se saca la longitud total. */
  while(1){

    //estos if anidados sirven para detectar si se está dentro de una cadena "", paréntesis, si un caracter está escapado en la cadena
    if(escaped){
      escaped = 0;
        } else if(quoted && *cursor == '\\'){
        escaped = 1;
            } else if(*cursor == '"'){
            quoted = !quoted;
                } else if(!quoted && *cursor == '('){
                parenthesis_depth++;
                    } else if(!quoted && *cursor == ')'){
                    parenthesis_depth--;
                    if(parenthesis_depth < 0)
                        return -1;
                    }

        /*CUando se detecta el final de un operando: esto se determina si se llega al final de la cadena, ósea "\0" o si se detecta una coma pero se está 
        fuera de un paréntesis y de comillas. EJ: a0, a1
                                                    ^
                                                    |
                                                    |*/
        if(*cursor == 0 || (!quoted && parenthesis_depth == 0 && *cursor == ',')){
        char saved;
        char *operand;
        int length;

        //Se guarda termporalmente la coma en "saved" y se sustitye la coma en la cadena por "\0".
        //al hacer esto, tengo el siguiente resultado: "start = a0\0 a1", ósea la cadena start en realidad  termina en \0 y no después de a1
        saved = *cursor;
        *cursor = 0;
        //lo siguiente que se hace es quitar los espacios (si hbuiera a la izquierda o a la deracha) de start que actualmente solo tiene el operando 
        //"  a0  " --> "a0"
        operand = trim_text(start);
        length = strlen(operand);

        //validio que el operando  no esté vacío, no haya más de 3 operandos actualmente y que el operando no sea demasiado largo
        //EJ inválido: add a0,,a2 ---> segundo operando vacío
        if(length <= 0 || count >= XV6_TCC_LINE_MAX_OPERANDS || length >= XV6_TCC_LINE_OPERAND_MAX)
            return -1;

        //copio el operando en el array de operandos y aumento el contador de operandos
        memmove(operands[count], operand, length + 1);
        count++;

        //si el caracter que guardé momentanemente en "ssaved" NO es una coma, entonces es el fin de línea \0, por lo que he llegado al final de la fila
        //y ya no hay más operandos
        if(saved == 0)
            break;

        //si el caracter salvado era la coma, entonces restauro la coma "a0\0 a1" -----> "a0, a1" 
        *cursor = saved;
        //muevo el comienzo de la línea hasta el siguiente operando
        start = skip_spaces(cursor + 1);
    }

    //avanzo el cursor antes de la siguiente iteración
    if(*cursor == 0)
      break;
    cursor++;
  }

  /*Se valida: se rechaza si hay una cadena sin cerrar (con comillas) o si hay un paréntesis sin cerrar
  EJ inválidos: 
  .asciz "Hola
   ld a0, 16(sp*/
  if(quoted || parenthesis_depth != 0)
    return -1;

  //si todo funciona se retorna el número de operandos
  return count;
}

/*BUsca una directiva en la tabla de directivas*/
static const struct Xv6TccDirectiveDescription *
find_directive(const char *name)
{
  int i;

  /*se recorre la tabla de directivas hasta encontrar una entrada que se corresponda con la directiva en "name", 
  si se encuentre se devuelve el puntero a esa entrada 
  EJ: ".word" ------> devuelve la entrada { ".word", 1, 3 }*/
  for(i = 0; i < (int)(sizeof(directives) / sizeof(directives[0])); i++){
    if(strcmp(name, directives[i].name) == 0)
      return &directives[i];
  }

  //si no encuentra ninguna directiva devuelve cero
  return 0;
}

/*Función principal del parser: 
Recorre toda una línea para identificar todos los elementos*/
int
xv6_tcc_parse_line(const char *text, struct Xv6TccParsedLine *line)
{
  char buffer[XV6_TCC_LINE_TEXT_MAX]; //aquí se copia la línea completa porque se modificará
  char *cursor; //cursor para recorrer la línea
  char *colon; //puntero por si se encuentran ":" y por lo tanto se ha encontrado una etiqueta en la línea
  char *rest; //apuntará al resto del texto principal después de encontrar la primera instrucción
  int count;

  //se copia la línea en el buffer, se rechaza si la línea es nula o es demasiada larga o si el struct (Xv6TccParsedLine) donde se pondrán 
  //el análisis de la línea es nulo
  if(!line || copy_source_line(text, buffer, sizeof(buffer)) < 0)
    return -1;

  //la estructura de análisis de la línea se pone todo a cero
  memset(line, 0, sizeof(*line));
  //el campo del tipo de línea ya está a 0 de la instrucción anterior pero lo vuelvo a poner por claridad
  line->kind = XV6_TCC_LINE_EMPTY;

  //elimino los comentarios de la copia de la línea Ej: loop: add a0,a1,a2 # comentario  ---> loop: add a0,a1,a2 
  remove_comment(buffer);
  //elimino espacios a los lados EJ: "  loop: add a0,a1,a2   " ---> "loop: add a0,a1,a2"
  cursor = trim_text(buffer);

  //si la línea al quitar espacios queda completamente vacía, pues se devuelve eso
  if(*cursor == 0)
    return 0;

  //se recorre la cadena para ver si hay una etiqueta ("loop:")
  colon = find_label_colon(cursor);
  //si hay una etiqueta entonces colon = ":"
  if(colon){

    char *label;
    //se transforman momentanemente los ":" en un nulo, así:
    //loop: add... ---> loop\0 add...
    *colon = 0;
    //se eliminan los espacios alrededor del nombre de la etiqueta
    label = trim_text(cursor);
    //se comprueba que el nombre sea válido y que queda en el campo label de la struct de análisis de la isntrucción
    if(!xv6_tcc_valid_identifier(label) || copy_name(label, line->label, sizeof(line->label)) < 0)
      return -1;

    //en el struct de anális se marca que tiene una label 
    line->has_label = 1;
    //se avanza a la siguiente parte de la línea (después de la etiqueta) y se quitan espacios ---> "add a0,a1,a2"
    cursor = trim_text(colon + 1);
    //si la línea era la etiqueta sola "loop:" se termina la función
    if(*cursor == 0)
      return 0;
  }

  //actualmente en el EJ cursor apunta al inicio de "add a0,a1,a2"

  //se saca el nombre de la instrucción, ósea el comando de la operación: para add a0,a1,a2 ---> line->name = "add", rest = "a0,a1,a2""
  rest = first_token(cursor, line->name, sizeof(line->name));
  if(!rest)
    return -1;

  //separo los operandos y los guardo en el array de operandos y tb saco el número de operandos.
  /*Ej: rest = "a0, a1, 42"
  entonces 
    count = 3
    operands[0] = "a0"
    operands[1] = "a1"
    operands[2] = "42"*/
  count = split_operands(rest, line->operands);
  if(count < 0)
    return -1;
  line->operand_count = count;

  //sse determina si el primer nombre extraído de la línea es una directiva, es decir, inica por "."    EJ: line->name = ".word"
  if(line->name[0] == '.'){

    /*SI es una directiva, se busca en la tabla de directivas una que se corresponda con ella*/
    const struct Xv6TccDirectiveDescription *directive;
    directive = find_directive(line->name);

    /*Si el número de operandos de la directiva no se corresponde con el mínimo o máximo de operandos, es inválido.
    EJ: ".word" ---> inválido, .word necesita al menos un operando como ".word 42"*/
    if(!directive || count < directive->minimum_operands || count > directive->maximum_operands)
      return -1;

    line->kind = XV6_TCC_LINE_DIRECTIVE;
    return 0;
  }

  //se válida la instrucción (el primer nombre extraído, en line->name). Esto se hace si la línea no era una directiva
  {
    const struct Xv6TccInstruction *instruction;

    //se busca la directiva en la tabla de directivas
    instruction = xv6_tcc_find_instruction(line->name);
    //se rechaza la instrucción si no existe, o si el número de operandos es incorrecto
    if(!instruction || count != instruction->operand_count)
      return -1;
  }

  line->kind = XV6_TCC_LINE_INSTRUCTION;
  return 0;
}

/*Esta función recibe el struct de una línea ya analizada y solo acepta instrucciones*/
int
xv6_tcc_encode_parsed_instruction(const struct Xv6TccParsedLine *line,
                                   uint *word)
{
  const char *operand1;
  const char *operand2;
  const char *operand3;

  //no se acepta si no se recibe una linea analizada que sea una instrucción o si se mandan punteros nulos
  if(!line || !word || line->kind != XV6_TCC_LINE_INSTRUCTION)
    return -1;

  /*Se sacan los operandos de la línea analizada. COmo hay instrucciones con menos de 3 operadoes, en ese caso 
  se pone 0 en ese operando, porque se va a llamar luego a la función para codificar en 32 la instrucción*/
  operand1 = line->operand_count > 0 ? line->operands[0] : 0;
  operand2 = line->operand_count > 1 ? line->operands[1] : 0;
  operand3 = line->operand_count > 2 ? line->operands[2] : 0;


  //se llama a la función para codificar la instrucción 32 bits
  return xv6_tcc_encode_named_instruction(line->name,
                                           operand1,
                                           operand2,
                                           operand3,
                                           word);
}

/*codifica la instrucción ya clasificada en xv6_tcc_parse_line y además escribe los 4 bytes (32 bits) de la instrucción binaria 
en el buffer que representa la sección .text, todavía no en el fichero ELF definitivo en disco*/
int
xv6_tcc_emit_parsed_instruction(const struct Xv6TccParsedLine *line,
                                 struct Xv6TccElfBuffer *text_section,
                                 uint *offset)
{
  uint word;

  if(!text_section || !offset ||
     xv6_tcc_encode_parsed_instruction(line, &word) < 0)
    return -1;

  return xv6_tcc_emit32(text_section, word, offset);
}
