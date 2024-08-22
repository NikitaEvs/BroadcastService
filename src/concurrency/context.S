.global SetContext
.global SwapContext 

SetContext:
    # Arguments
    # 1) %rdi - pointer to the Context structure    
    # 2) %rsi - pointer to the IRunnable structure

    # Set rsp
    # rsp = Context::rsp
    movq %rsp, %r8
    movq 8*1(%rdi), %rsp

    # Red zone
    subq $128, %rsp

    # Align stack
    andq $-16, %rsp
    addq $8, %rsp

    # 7-th argument for ContextRun 
    movq %rsi, 8(%rsp)

    # Push registers to the stack (rbx, rbp, r12-15)
    pushq $0
    pushq $0
    pushq $0
    pushq $0
    pushq $0
    pushq $0

    # Save rsp to the Context
    movq %rsp, 8*1(%rdi)

    # Restore rsp
    movq %r8, %rsp

    retq

SwapContext:
    # Arguments
    # 1) %rdi - pointer to the old Context structure
    # 2) %rsi - pointer to the new Context structure 

    # Save return address to old Context::rip
    movq (%rsp), %r8
    movq %r8, 8*0(%rdi)

    addq $0x8, %rsp

    # Save callee-saved registers to the stack
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # Save rsp to the old Context::rsp
    movq %rsp, 8*1(%rdi)

    # Save a return address of the Context to the register
    # tmp = Context::rip
    movq 8*0(%rsi), %r8

    # Set new rsp
    # rsp = Context::rsp
    movq 8*1(%rsi), %rsp

    # Set callee-saved register from the stack
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    # Push an rip on the stack, so it will be used in the retq
    pushq %r8

    retq
