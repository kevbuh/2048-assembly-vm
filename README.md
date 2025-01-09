# assembly-vm

emulates https://en.wikipedia.org/wiki/Little_Computer_3

https://www.jmeiners.com/lc3-vm/#:memory-mapped-registers

### LC-3 Assembly Hello World

```
.ORIG x3000                        ; memory address where program will be loaded
LEA R0, HELLO_STR                  ; load address of HELLO_STR string into R0
PUTs                               ; output string pointed to by R0 to console
HALT                               ; halt program
HELLO_STR .STRINGZ "Hello World!"  ; store string here in the program
.END                               ; mark end of the file
```