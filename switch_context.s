# switch_context.s
.intel_syntax noprefix          # Use Intel syntax (much easier to read)

.global switch_context

switch_context:
	# save register values to 1st arg
	mov [rdi + 0], rbx;
	mov [rdi + 8], rsp;
	mov [rdi + 16], rbp;
	mov [rdi + 40], r12;
	mov [rdi + 48], r13;
	mov [rdi + 56], r14;
	mov [rdi + 64], r15;

	# save the instruction ptr by reading the value at the top of the stack
	mov rax, [rsp];
	mov [rdi + 72], rax;

	# load new register values
	mov rbx, [rsi + 0];
	mov rsp, [rsi + 8];
	mov rbp, [rsi + 16];
	mov r12, [rsi + 40];
	mov r13, [rsi + 48];
	mov r14, [rsi + 56];
	mov r15, [rsi + 64];

	# load new instruction into rax
	mov rax, [rsi + 72];

	# switch stack
	mov rsp, [rsi + 8];

	# load new rsi (2nd arg)
	mov rsi, [rsi + 32];

	# jump to new instruction
	jmp rax;

