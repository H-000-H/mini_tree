/* default stack size */
__mini_os_stack_size = 0x800;     /* 2KB，change by youself need */

. = ALIGN(8);
__mini_os_heap_start = .;

. = ORIGIN(RAM) + LENGTH(RAM) - __mini_os_stack_size;
. = ALIGN(8);
__mini_os_heap_end = .;/*stack check address is a sentinel magic number set before main default 0x060813 */

_estack = ORIGIN(RAM) + LENGTH(RAM);
