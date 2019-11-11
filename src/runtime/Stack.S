/******************************************************************************
    Copyright (C) Martin Karsten 2015-2019

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
.include "runtime/RegistersOnStack.HS"

.text

.ifdef _try__i386__

.align 4
.globl stackSwitch
stackSwitch:                        # (currStack, postFunc, &currSP, nextSP)
	STACK_PUSH                        # save register context
	movl (STACKFRAME+ 4)(%esp), %eax  # load currStack
	movl (STACKFRAME+ 8)(%esp), %ecx  # load postFunc
	movl (STACKFRAME+12)(%esp), %edx  # load currSP
	movl %esp, (%edx)                 # save current stack pointer into currSP
	movl (STACKFRAME+16)(%esp), %esp  # load nextSP into stack pointer
	STACK_POP                         # restore register context
	movl %eax, 4(%esp)                # set currStack argument for postFunc
	jmp *%exc                         # return directly from postFunc

.endif

.align 8
.globl stackDirect
stackDirect:                # stack pointer, func, arg1, arg2, arg3
	movq %rdi, %rsp           # set stack pointer
	movq %rsi, %rax           # save 'func' in %rax
	movq %rdx, %rdi           # put arg1 in place
	movq %rcx, %rsi           # put arg2 in place
	movq %r8,  %rdx           # put arg3 in place
#	movq %r9,  %rcx           # put arg4 in place # not needed
	pushq $0                  # previous %rip = 0 (fake stack frame)
	pushq $0                  # previous %rbp = 0
	movq %rsp, %rbp           # set base pointer
	jmp *%rax                 # invoke 'func'

.align 8
.globl stackInit
stackInit:                  # stack, func, arg1, arg2, arg3 -> new stack
	movq %rsi, -64(%rdi)      # store 'func' for stub function (via %rbx)
	movq %rdx, -56(%rdi)      # store 'arg1' for stub function (via %r12)
	movq %rcx, -48(%rdi)      # store 'arg2' for stub function (via %r13)
	movq %r8,  -40(%rdi)      # store 'arg3' for stub function (via %r14)
	movq $0,   -32(%rdi)      # indirectly set %r15 to 0
	movq $0,   -24(%rdi)      # indirectly set %rbp to 0
#	movq $stubInit, -16(%rdi) # push stubInit function as return address
	leaq stubInit(%rip), %rax # alternative: use RIP-relative addressing
	movq %rax, -16(%rdi)      # to push stubInit function as return address
	leaq -64(%rdi), %rax      # return stack address, size 64: cf. STACK_PUSH
	retq

.align 8
stubInit:                   # stub needed to pass arguments to 'invokeStack'
	movq %rbx, %rdi           # 'func'
	movq %r12, %rsi           # 'arg1'
	movq %r13, %rdx           # 'arg2'
	movq %r14, %rcx           # 'arg3'
#	movq %r15, %r8            # 'arg4' not needed
	pushq %rbp                # previous %rip = 0 (fake stack frame)
	pushq %rbp                # previous %rbp = 0
	movq %rsp, %rbp           # set base pointer
	jmp invokeStack@PLT

.align 8
.globl stackSwitch
stackSwitch:                # (currStack, postFunc, &currSP, nextSP)
	STACK_PUSH                # save register context
	movq %rsp, (%rdx)         # save current stack pointer
	movq %rcx, %rsp           # load next stack pointer
	STACK_POP                 # restore register context
	jmp *%rsi                 # %rdi set -> return directly from postFunc
