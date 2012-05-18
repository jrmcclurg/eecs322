#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// please don't remove the function prototypes for now!
// it prevents annoying compile warnings
extern void *allocate(int, void*);
void *allocate_helper(int, void*, int*,int*,int*);
int   print(void*);
int   print_error(int*, int);
void  print_content(void**, int);
int  *gc_copy(int*);
void  gc(int *esp);

#define HEAP_SIZE 1048576  // one megabyte
//#define HEAP_SIZE 50       // small heap size for testing
#define ENABLE_GC            // uncomment this to enable GC

void** heap;           // the current heap
void** heap2;          // the heap for copying
void** heap_temp;      // a pointer used for swapping heap/heap2

int * allocptr;       // current allocation position
int words_allocated = 0;

int *stack; // pointer to the bottom of the stack (i.e. value
            // upon program startup)

/*
 * Helper for the print() function
 */
void print_content(void** in, int depth) {
   if (depth >= 4) {
      printf("...");
      return;
   }
   int x = (int) in;
   if (x&1) {
      printf("%i",x>>1);
   } else {
      int size= *((int*)in);
      void** data = in+1;
      int i;
      printf("{s:%i", size);
      for (i=0;i<size;i++) {
         printf(", ");
         print_content(*data,depth+1);
         data++;
      }
      printf("}");
   }
}

/*
 * Runtime "print" function
 */
int print(void* l) {
   print_content(l,0);
   printf("\n");

   return 1;
}

/*
 * Helper for the gc() function.
 * Copies (compacts) an object from the old heap into
 * the empty heap
 */
int gc_copy( int *old )  {
    // If not a pointer or not a pointer to a heap location, return input value
    if ( old % 4 != 0 || (void**)old < heap2 && (void**)old >= heap2 + HEAP_SIZE )
        return old;

    int * oldArray = (int *)old;
    int size = oldArray[0];

    // If the size is negative, the array has already been copied to the
    // new heap, so the first location of array will contain the new address
    if ( size == -1 )
        return oldArray[1];

    // Mark the old array as invalid, create the new array
    oldArray[0] = -1;
    int * newArray = allocptr;
    allocptr += size + 1;
    words_allocated += size + 1;

    // The value of oldArray[1] needs to be handled specially
    int firstArrayLocation = oldArray[1];
    oldArray[1] = (int)newArray;

    // Set the values of newArray handling the first two locations separately
    newArray[0] = size;
    newArray[1] = gc_copy( firstArrayLocation );

    // Call gc_copy on the remaining values of the array
    for ( int i = 2; i <= size; i++ )
        newArray[i] = gc_copy( oldArray[i] );

    return (int)newArray;
}

/*
 * Initiates garbage collection
 */
inline void gc(int * esp, int *edi, int *esi) {
   // calculate the stack size
   int stack_size = stack - esp;

   //printf("Garbage collection: stack=%p, esp=%p, num=%d\n", stack, esp, num);

   // swap in the empty heap to use for storing
   // compacted objects
   heap_temp = heap;
   heap = heap2;
   heap2 = heap_temp;

   // reset heap position
   allocptr = (int *)heap;
   words_allocated = 0;

   // First, do the garbage collection on the callee save registers
   *edi = gc_copy( edi );
   *esi = gc_copy( esi );

   // Then, we need to copy anything pointed at
   // by the stack into our empty heap
   for( int i = 0; i <= stack_size; i++ )
      esp[i] = gc_copy( esp[i] );
}

/*
 * The "allocate" runtime function
 * (assembly stub that calls the 5-argument
 * allocate_helper function)
 */
asm(
   "   .globl	allocate\n"
   "   .type	allocate, @function\n"
   "allocate:\n"
   "# grab the arguments (into eax,edx)\n"
   "popl %ecx\n"
   "popl %eax\n"
   "popl %edx\n"
   "\n"
   "# (put them back on stack)\n"
   "pushl %edx\n"
   "pushl %eax\n"
   "pushl %ecx\n"
   "# save the original esp (into ecx)\n"
   "movl %esp, %ecx\n"
   "# save the caller's base pointer (so that LEAVE works)\n"
   "push %ebp\n"
   "movl %esp, %ebp\n"
   "# body begins with base and\n"
   "# stack pointers equal\n"
   "# call the real alloc\n"
   "subl $8, %esp\n"
   "subl $4, %ebp\n"
   "movl %esi, (%ebp)\n"
   "pushl %ebp\n"
   "subl $4, %ebp\n"
   "movl %edi, (%ebp)\n"
   "pushl %ebp\n"
   "addl $8, %ebp\n"
   "pushl %ecx\n"
   "pushl %edx\n"
   "pushl %eax\n"
   "call allocate_helper\n"
   "addl	$20, %esp\n"
   "addl	$8, %esp\n"
   "\n"
   "leave\n"
   "ret\n" 
);

/*
 * The "allocate" runtime function
 */
void* allocate_helper( int fw_size, void *fw_fill, int *esp, int *edi, int *esi )
{
    if ( !( fw_size & 1 ) ) {
        printf("allocate called with size input that was not an encoded integer, %i\n",
                fw_size);
    }

    int dataSize = fw_size >> 1;

    if ( dataSize < 0 ) {
        printf( "allocate called with size of %i\n", dataSize );
        exit( -1 );
    }

    // Even if there is no data, allocate an array of two words
    // so we can hold a forwarding pointer and an int representing if
    // the array has already been garbage collected
    int arraySize = ( dataSize == 0 ) ? 2 : dataSize + 1;

    // Check if the heap has space for the allocation
    if ( words_allocated + dataSize < HEAP_SIZE )
    {
        // Garbage collect
        gc(esp, edi, esi);

        // Check if the garbage collection free enough space for the allocation
        if ( words_allocated + dataSize < HEAP_SIZE ) {
            printf("out of memory");
            exit(-1);
        }
    }

    // Do the allocation
    int * ret = allocptr;
    allocptr += arraySize;
    words_allocated += arraySize;

    // Set the size of the array to be the desired size
    ret[0] = dataSize;

    // If there is no data, set the value of the array to be a number
    // so it can be properly garbage collected
    if ( dataSize == 0 )
        ret[1] = 1;
    else
        // Fill the array with the fill value
        memset( &ret[1], (int)fw_fill, dataSize );

    return ret;
}

/*
 * The "print-error" runtime function
 */
int print_error(int* array, int fw_x) {
   printf("attempted to use position %i in an array that only has %i positions\n",
		 fw_x>>1, *array);
   exit(0);
}

/*
 * Program entry-point
 */
int main() {

   heap = (void*)malloc(HEAP_SIZE*sizeof(void*));
   heap2 = (void*)malloc(HEAP_SIZE*sizeof(void*));
   allocptr = heap;
   // NOTE: allocptr needs to appear in the following check, because otherwise
   // gcc apparently optimizes away the assignment (i.e. the allocate_helper function
   // sees allocptr as NULL)
   if (!allocptr || !heap2) {
      printf("malloc failed\n");
      exit(-1);
   }
   // TODO - set callee save here
   // move esp into the bottom-of-stack pointer
   // the "go" function's boilerplate (if copied correctly from the
   // lecture notes), in conjunction with the C calling convention
   // dictates that there will be exactly 6 words added to the
   // stack before the body of "go" actually happens
   asm ("movl %%esp, %%eax;"
        "subl $24, %%eax;" // 6 * 4
        "movl %%eax, %0;"
        "call go;"
      : "=m"(stack) // outputs
      :             // inputs (none)
      : "%eax"      // clobbered registers (none)
   );  
   printf("Main got stack : %d\n", stack);
   return 0;
}
