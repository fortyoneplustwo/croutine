###### Plan
# 1. Check the current list element (%eax) to see if it’s zero (the terminating element).
# 2. If it is zero, exit.
# 3. Increase the current position (%edi).
# 4. Load the next value in the list into the current value register (%eax). What
# addressing mode might we use here? Why?
# 5. Compare the current value (%eax) with the current highest value (%ebx).
# 6. If the current value is greater than the current highest value, replace the
# current highest value with the current value.
# 7. Repeat.

# edi holds index in list (i)
# ebx holds max so far
# eax holds curr element of the list

# The following memory locations are used:
#
# data_items - contains the item data. A 0 is used to terminate the data


.section .data
data_items: #These are the data items
	.long 3,67,34,222,45,75,54,34,44,33,22,11,66,0

.section .text

.globl _start

_start:
	movl $0, %edi # move 0 into the index register
	movl data_items(,%edi,4), %eax # load the first byte of data
	movl %eax, %ebx # since this is the first item, %eax is the biggest

start_loop: # start loop
	cmpl $0, %eax # check to see if we’ve hit the end (eax - ebx)
	je loop_exit
	incl %edi # load next value # increment edi
	movl data_items(,%edi,4), %eax
	cmpl %ebx, %eax # compare values
	jle start_loop # jump to loop beginning if the new one isn’t bigger

	movl %eax, %ebx # move the value as the largest

	jmp start_loop # jump to loop beginning

loop_exit:
	# %ebx is the status code for the exit system call
	# and it already has the maximum number
	movl $1, %eax #1 is the exit() syscall
	int $0x80


