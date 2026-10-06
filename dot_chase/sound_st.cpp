/* Dot Chase - ST port: sound.c (direct Paula register writes) built as
 * C++ so that dmacon writes reach the Paula emulation
 * (../st_port/paula.h), with C linkage for the rest of the game. */
extern "C" {
#include "sound.c"
}
