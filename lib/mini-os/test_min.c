#ifdef CONFIG_X
#define MINI_OS_X CONFIG_X /**< max thread name length */
#elif defined(MINI_OS_X)
/* pre */
#else
#define MINI_OS_X 32 /**< max thread name length */
#endif
