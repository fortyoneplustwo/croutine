.section .data
.section .text
.globl _start

_start:
	movl $1, %eax # put exit syscall number in eax

	movl $0, %ebx # put status code 0 in ebx (required by exit syscall)

	int $0x80 # call interupt number
