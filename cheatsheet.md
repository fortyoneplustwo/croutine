# Assembly

## immediate addressing

data to access is embedded in the intruction itself as a constant

## register accessing

data to access is in a register rather than a memory location

## indexed addressing

instruction contains:

- a memory address to access (constant)
- an index register to offset that address
- a multiplier for the index (size of the data)

## indirect addressing mode

instruction contains

- a register that points to a mem addr where the data is stored

## base pointer addressing

like indirect addressing but also with an offset that to add to the register's value before lookup

- register contains a value to the base pointer

# Registers

## eax, ebx, ecx, edx, edi, esi

general purpose

## eip, eflags

can only be accessed through special instructions

## esp, eip

stack and base pointers

# Instructions

## movl $1, %eax

moves the number 1 to eax

# Adressing modes

## ADDRESS_OR_OFFSET(%BASE_OR_OFFSET,%INDEX,MULTIPLIER)

- all fields are optional
- FINAL ADDRESS = ADDRESS_OR_OFFSET + %BASE_OR_OFFSET + MULTIPLIER \* %INDEX
- ADDRESS_OR_OFFSET and MULTIPLIER must both be constants
- %BASE_OR_OFFSET and %INDEX must be registers

## direct addressing

`movl ADDRESS %eax`

Move value at ADDRESS to eax

## indexed addressing

`movl STR_ADDRESS(,%eax, 1), %ebx`

Move value at STR_ADDRESS + value at eax to ebx

## indirect addressing mode

`movl (%ebx), %eax`

Move value stored at the address stored in ebx to eax

## base pointer addressing

`movl 4(%eax) %ebx`

Move value at memory address of eax + 4 to ebx
4 is the offset
eax is the base

## immediate mode

`movl $12, %eax`

## register addressing

`movl %eax, %ebx`

Move value in eax to ebx
