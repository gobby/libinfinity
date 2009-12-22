/* Fake unistd.h header to get the ssize_t definition for MSVC */

#include <stddef.h>
typedef intptr_t ssize_t;
