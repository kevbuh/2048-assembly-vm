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

// TRAP CODES
enum
{
    TRAP_GETC = 0x20,  // get character from keyboard, not echoed onto the terminal
    TRAP_OUT = 0x21,   // output a character
    TRAP_PUTS = 0x22,  // output a word string
    TRAP_IN = 0x23,    // get character from keyboard, echoed onto the terminal
    TRAP_PUTSP = 0x24, // output a byte string
    TRAP_HALT = 0x25   // halt the program
};

// MEMORY ARRAY
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];  // 65536 mem locations

// MEMORY MAPPED REGISTERS
enum
{
    MR_KBSR = 0xFE00, // keyboard status
    MR_KBDR = 0xFE02  // keyboard data
};

// GENERAL PURPOSE REGISTERS
// 10 total, each 16 bits
enum
{
    
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
    OP_LDI,    // load indirect (load a value from a location in memory into a register)
    OP_STI,    // store indirect
    OP_JMP,    // jump
    OP_RES,    // reserved (unused)
    OP_LEA,    // load effective address
    OP_TRAP    // execute trap
};

// CONDITION FLAGS
enum
{
    FL_POS = 1 << 0, // P (positive)
    FL_ZRO = 1 << 1, // Z (zero)
    FL_NEG = 1 << 2, // N (negative)
};

// INPUT BUFFERING
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO; // Disable canonical mode and echoing
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

// Sign extension corrects this problem by filling in 0’s for positive numbers and 1’s for negative numbers
// original values are preserved
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) { // if negative number
        x |= (0xFFFF << bit_count); // fill with 1's
    }
    return x;
}

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) // 1 in the left-most bit indicates negative
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    // the origin tells us where in memory to place the image
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    // we know the maximum file size so we only need one fread
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // swap to little endian
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}


uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15); // set the "keyboard ready" bit
            memory[MR_KBDR] = getchar(); // read the character
        }
        else
        {
            memory[MR_KBSR] = 0; // clear the "keyboard ready" bit
        }
    }
    return memory[address];
 }

int main(int argc, const char* argv[])
{
    // LOAD ARGS
    if (argc < 2) {
        // show usage string
        printf("Not enough arguments! ex: ./lc3-vm 2048.obj\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j])) {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    // SETUP
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    // since exactly one condition flag should be set at any given time, set the Z flag
    reg[R_COND] = FL_ZRO;

    // set the PC to starting position
    // 0x3000 is the default
    enum { PC_START = 0x3000 }; // lower addresses are left empty to leave space for the trap routine code
    reg[R_PC] = PC_START;

    // LOOP
    int running = 1;
    while (running) {
        
        // FETCH INSTR AND GET OP
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                {
                    uint16_t r0 = (instr >> 9) & 0x7; // destination register (DR)   
                    uint16_t r1 = (instr >> 6) & 0x7; // first operand (SR1)

                    // immediate mode flag
                    uint16_t imm_flag = (instr >> 5) & 0x1;
                    if (imm_flag) {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] + imm5;
                    }
                    else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] + reg[r2];
                    }

                    update_flags(r0);
                    break;
                }
            case OP_AND:
                {
                    uint16_t r0 = (instr >> 9) & 0x7; // destination register (DR)
                    uint16_t r1 = (instr >> 6) & 0x7; // first operand (SR1)

                    uint16_t imm_flag = (instr >> 5) & 0x1;
                    if (imm_flag) {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        reg[r0] = reg[r1] & imm5;
                    }
                    else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] & reg[r2];
                    }

                    update_flags(r0);
                    break;
                }
            case OP_NOT:
                {
                    uint16_t r0 = (instr >> 9) & 0x7; // destination register (DR)
                    uint16_t r1 = (instr >> 6) & 0x7; // first operand (SR1)
                    
                    reg[r0] = ~reg[r1];
                    update_flags(r0);
                    break;
                }
            case OP_BR:
                {
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    uint16_t cond_flag = (instr >> 9) & 0x7; // n,z,p
                    if (cond_flag & reg[R_COND]) {
                        reg[R_PC] += pc_offset;
                    }
                    break;
                }
            case OP_JMP:
                {
                    // also handles RET
                    uint16_t base_reg = (instr >> 6) & 0x7; // n,z,p
                    reg[R_PC] = reg[base_reg];
                    break;
                }
            case OP_JSR:
                {
                    uint16_t long_flag = (instr >> 11) & 1;
                    reg[R_R7] = reg[R_PC];
                    if (long_flag) {
                        uint16_t pc_offset_11 = sign_extend(instr & 0x7FF, 11);
                        reg[R_PC] += pc_offset_11;  // JSR
                    }
                    else {
                        uint16_t base_reg = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[base_reg]; // JSRR
                    }
                    break;
                }
            case OP_LD:
                {
                    uint16_t dr = (instr >> 9) & 0x7; // destination register (DR)
                    uint16_t pc_offset_9 = sign_extend(instr & 0x1FF, 9);

                    reg[dr] = mem_read(reg[R_PC] + pc_offset_9);
                    update_flags(dr);
                    break;
                }
            case OP_LDI: 
                {
                    uint16_t r0 = (instr >> 9) & 0x7; // destination register
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9); // PC_offset_9 
                    // add pc_offset to the current PC, look at that memory location to get the final address
                    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));

                    update_flags(r0);
                    break;
                }
            case OP_LDR:
                {   
                    uint16_t dr = (instr >> 9) & 0x7; // destination register
                    uint16_t br = (instr >> 6) & 0x7; // base register
                    uint16_t pc_offset_6 = sign_extend(instr & 0x3F, 6);

                    reg[dr] = mem_read(reg[br] + pc_offset_6);
                    update_flags(dr);
                    break;
                }
            case OP_LEA:
                {
                    uint16_t dr = (instr >> 9) & 0x7; // destination register
                    uint16_t pc_offset_9 = sign_extend(instr & 0x1FF, 9);
                    
                    reg[dr] = reg[R_PC] + pc_offset_9;
                    update_flags(dr);
                    break;
                }
            case OP_ST:
                {
                    uint16_t br = (instr >> 9) & 0x7;
                    uint16_t pc_offset_9 = sign_extend(instr & 0x1FF, 9);
                    mem_write(reg[R_PC] + pc_offset_9, reg[br]);
                    break;
                }
            case OP_STI:
                {
                    uint16_t br = (instr >> 9) & 0x7;
                    uint16_t pc_offset_9 = sign_extend(instr & 0x1FF, 9);
                    mem_write(mem_read(reg[R_PC] + pc_offset_9), reg[br]);
                    break;
                }
            case OP_STR:
                {
                    uint16_t br = (instr >> 9) & 0x7;
                    uint16_t sr = (instr >> 6) & 0x7; // source register
                    uint16_t offset6 = sign_extend(instr & 0x3F, 6);
                    mem_write(reg[sr] + offset6, reg[br]);
                    break;
                }
            case OP_TRAP:
                {
                    reg[R_R7] = reg[R_PC];

                    switch (instr & 0xFF)
                    {
                        case TRAP_GETC: // read a single ASCII char
                            {
                                reg[R_R0] = (uint16_t)getchar();
                                update_flags(R_R0);
                                break;
                            }
                        case TRAP_OUT: // output a character
                            {
                                putc((char)reg[R_R0], stdout);
                                fflush(stdout);
                                break;
                            }
                        case TRAP_PUTS: // output a null terminated string
                            {
                                uint16_t* c = memory + reg[R_R0];
                                while (*c)
                                {
                                    putc((char)*c, stdout);
                                    ++c;
                                }
                                fflush(stdout);
                                break;
                            }
                        case TRAP_IN: // input character
                            {
                                printf("*** Enter a character: ");
                                char c = getchar();
                                printf("\nRead character: %c\n", c); // Debug print
                                putc(c, stdout);
                                fflush(stdout);
                                reg[R_R0] = (uint16_t)c;
                                update_flags(R_R0);
                                break;

                            }
                        case TRAP_PUTSP: // output string
                            {
                                /* one char per byte (two bytes per word)
                                here we need to swap back to big endian format */
                                uint16_t* c = memory + reg[R_R0];
                                while (*c)
                                {
                                    char char1 = (*c) & 0xFF;
                                    putc(char1, stdout);
                                    char char2 = (*c) >> 8;
                                    if (char2) putc(char2, stdout);
                                    ++c;
                                }
                                fflush(stdout);
                                break;
                            }
                        case TRAP_HALT: // halt program
                            {
                                puts("Thanks for playing!");
                                fflush(stdout);
                                running = 0;
                                break;
                            }
                    }
                    break;
                }
            case OP_RES:
            case OP_RTI:
            default:
                { 
                    abort();
                    break;
                }
        }
    }

    restore_input_buffering();
}