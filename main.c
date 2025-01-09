#include <stdio.h>
#include <stdint.h>
#include <signal.h>
// unix only
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>


#define MEMORY_MAX (1 << 16)

// MEMORY ARRAY
uint16_t memory[MEMORY_MAX];  // 65536 mem locations

// REGISTERS
// 10 total, each 16 bits
enum
{
    // general purpose
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    // program counter, address of the next instruction in memory to execute
    R_PC,
    // condition flag, tell us information about the previous calculation
    R_COND,
    // num registers
    R_COUNT
};

uint16_t reg[R_COUNT];

// OPCODES
enum
{
    OP_BR = 0, // branch
    OP_ADD,    // add 
    OP_LD,     // load
    OP_ST,     // store
    OP_JSR,    // jump register
    OP_AND,    // bitwise and
    OP_LDR,    // load register
    OP_STR,    // store register
    OP_RTI,    // unused
    OP_NOT,    // bitwise not
    OP_LDI,    // load indirect
    OP_STI,    // store indirect
    OP_JMP,    // jump
    OP_RES,    // reserved (unused)
    OP_LEA,    // load effective address
    OP_TRAP    // execute trap
};

// CONDITION FLAGS
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};