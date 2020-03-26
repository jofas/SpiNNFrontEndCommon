#include <sark.h>

extern uint STACK_TOP;
extern uint STACK_SIZE;
#define STACK_BOTTOM (STACK_TOP - STACK_SIZE)

static inline void stack_check(void) {
    register uint sp asm("sp");
    register uint lr asm("lr");
    if (sp < STACK_BOTTOM) {
        io_printf(IO_BUF, "Stack overflow at %u!", lr);
        rt_error(RTE_SWERR);
    }

}
