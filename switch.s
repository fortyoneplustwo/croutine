##### PURPOSE:
# Performs a context switch by saving the current
# execution state into _old and restoring the
# execution state from _new, such that when this
# function returns, execution continues from where
# 'new' left off.
#
##### INPUT: 
# First argument (_old) - old context
# Second argument (_new) - new context
#
##### OUTPUT: 
# None
#
##### NOTES: 
# 1. _old and _new are non-null, valid pointers
#    to properly allocated Context structs
# 2. The stack pointer stored in _new points to
#    a valid, mapped stack
# 3. The new stack is 16-byte aligned (required by
#    the System V AMD64 ABI)
# 4. The return address on _new's stack points to
#    a valid instruction to resume execution from
# 5. Caller-saved registers are the caller's
#    responsibility. This function only saves
#    and restores callee-saved registers:
#		 rbx, rbp, rsp, r12-15
#
# struct Context {
#		rsp
#		rbx
#		rbp
#		rb12
#		rb13
#		rb14
#		rb15
# }

##### VARIABLES:
# %rdi - pointer to _old 
# %rsi - pointer to _new

## Save curr context to _old
# save rsp
# save rbx
# save rbp
# save r12 to r15

## Load new context to _new
# load r12 to r12
# load rbx
# load rbp

## Load from _new's function parameters: rsi and rdi
## This is tricky because curr rsi and rdi already point to _old and _new
## ORDER MATTERS
# load rdi (safe to lose pointer to _old at this point)
# load rsi from rsi + offset to _new's rsi

###### Skip this part for now #######
## Push _new's local vars to the stack in reverse order
## (assume this is already done)
## can assume _new's stack already has its local vars pushed in reverse order
## onto its stack
#####################################

## Load from _new's rsp

## ret
## Need to use ret instruction to load rip automatically
## It pushes whatever is at the top of the stack into rip
## IMPORTANT:
# (both are done in C, before the call to the context switch)
# Make sure the top of the stack is the entry point to _new at this point!
# Make sure the bottom of the stack is pointing to a thread_exit() function!
#
# WARN:
# Stack needs to be 16-bytes aligned just before `call entry_func()`
# so that when execution has been transferred to entry_func(), rsp+8 = 16.
# This is bc `call` pushes a return address (8 bytes) onto the stack.
# May have to add padding at the very bottom
#
## Visually:
# high address
#   padding (8 bytes)
#   entry_func     <- context_switch's ret will jump to here
# low address      

.global switch_context

	switch_context:
	# save rsp
	movq %rsp, 0(%rdi)
	# save rbp
	movq %rbp, 8(%rdi)
	# save rbx
	movq %rbx, 16(%rdi)
	# save r12 to r15
	movq %r12, 24(%rdi)
	movq %r13, 32(%rdi)
	movq %r14, 40(%rdi)
	movq %r15, 48(%rdi)

# load rbp
	movq 8(%rsi), %rbp
# load rbx
	movq 16(%rsi), %rbx
# load r12
	movq 24(%rsi), %r12
	movq 32(%rsi), %r13
	movq 40(%rsi), %r14
	movq 48(%rsi), %r15

# load rdi (safe to lose pointer to _old at this point)
# movq 56(%rsi), %rdi
# load rsi from rsi + offset to _new's rsi
# movq 64(%rsi), %rsi

# switch to new's stack
	movq 0(%rsi), %rsp

	ret
