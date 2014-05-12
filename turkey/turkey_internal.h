#ifndef TURKEY_INTERNAL_H
#define TURKEY_INTERNAL_H
#include "turkey.h"

struct TurkeyGarbageCollectedObject {
	bool marked; /* has this item been marked for sweeping? */
	unsigned int hold; /* don't garbage collect if true */
	TurkeyGarbageCollectedObject *gc_next;
	TurkeyGarbageCollectedObject *gc_prev;
};

struct TurkeyClosure {
	TurkeyClosure *parent;
	unsigned long long int references;
};

struct TurkeyString : TurkeyGarbageCollectedObject {
	char *string;
	unsigned int length;
	unsigned long long int hash;
	TurkeyString *next; /* next string in a chain */
	TurkeyString *prev; /* previous string in a chain */
};

struct TurkeyStringTable {
	/* TurkeyStringTable is actually a hash table for storing strings in. */
	TurkeyString **strings; /* array of string pointers */
	unsigned int count; /* number of items in the string table */
	unsigned int length; /* length of the string table */
};

struct TurkeyFunctionPointer : TurkeyGarbageCollectedObject {
	TurkeyFunction *function;
	TurkeyClosure *closure;
};

struct TurkeyArray : TurkeyGarbageCollectedObject {
	unsigned int length; /* elements in the array */
	unsigned int allocated; /* allocated length of the array */
	TurkeyVariable *elements; /* elements of the array */
};

struct TurkeyBuffer : TurkeyGarbageCollectedObject {
	bool disposed; /* has it been disposed? */
	bool native; /* is it a native buffer? */
	void *ptr; /* pointer to the raw memory */
	size_t size; /* length of the buffer */
};

struct TurkeyObject : TurkeyGarbageCollectedObject {
};

struct TurkeyFunction {
	TurkeyNativeFunction native_function; /* 0 if this is a vm function */
	TurkeyModule *module; /* module this is from */
	void *start; /* pointer to the start of the bytecode for this function */
	void *end; /* pointer beyond the last bytecode for this function */
	unsigned int parameters; /* number of parameters this function expects */
	unsigned int closures; /* number of closure variables this function creates */
	unsigned int locals; /* number of local variables this function has */
};

struct TurkeyCallStackEntry {
	unsigned int return_function_index; /* function to return to */
	unsigned int return_position; /* position in the parent functin to return to */
	bool expects_return; /* the caller expects a return value pushed onto the stack */
	unsigned int return_variable_stack_top; /* top of the caller's variable stack */
	unsigned int return_local_stack_top; /* top of the caller's local stack */
	TurkeyClosure *return_closure; /* caller's closure */
};

struct TurkeyStack {
	unsigned int top; /* top of the stack for the current call frame */
	unsigned int position; /* current position */
	unsigned int length; /* length of the variables array */
	TurkeyVariable *variables; // array of variables
};

struct TurkeyCallStack {
	unsigned int current; /* current call stack */
	unsigned int length; /* size of the call stack */
	TurkeyCallStackEntry *entries; /* array of entries */
};

struct TurkeyFunctionArray {
	unsigned int count; /* number of functions */
	TurkeyFunction *functions; /* array of turkey functions */
};

struct TurkeyModule {
	unsigned long long int function_offset; /* offets to add to push functions for this module */
	unsigned long long int function_count; /* number of functions this module has in the function table */
	unsigned long long int string_offset; /* offsets to add to push strings for this module */
	unsigned long long int string_count; /* number of strings this module has in the string table */
};

struct TurkeyModuleArray {
	unsigned int count; /* number of loaded modules */
	TurkeyModule *functions; /* array of loaded modules */
};

struct TurkeyGarbageCollector {
	 /* pointers to linked list of items to collect */
	TurkeyString *strings;
	TurkeyBuffer *buffers;
	TurkeyArray *arrays;
	TurkeyObject *objects;
	TurkeyFunctionPointer *function_pointers;

	unsigned long long int items; /* number of allocated items */
};

struct TurkeyVM {
	bool debug; /* no jit */

	TurkeyModuleArray module_array; /* number of loaded modules */
	TurkeyStringTable string_table; /* table of strings */
	TurkeyFunctionArray function_array; /* array of functions */
	TurkeyCallStack call_stack; /* the call stack */
	TurkeyStack variable_stack; /* the variable stack, for operations */
	TurkeyStack local_stack; /* the local stack, that functions store variables into */
	TurkeyGarbageCollector garbage_collector;

	unsigned long long int current_function_index; /* index of the currently executing function, 0 is invalid and means returning to native code */
	unsigned long long int current_byteindex; /* index of the currently executing bytecode */
};

/* array.h */
extern TurkeyArray *turkey_array_new(TurkeyVM *vm, unsigned int size);
extern void turkey_array_delete(TurkeyVM *vm, TurkeyArray *arr);
extern void turkey_array_push(TurkeyVM *vm, TurkeyArray *arr, const TurkeyVariable &variable);
extern void turkey_array_grow(TurkeyVM *vm, TurkeyArray *arr);
extern void turkey_array_allocate(TurkeyVM *vm, TurkeyArray *arr, unsigned int size);
extern void turkey_array_resize(TurkeyVM *vm, TurkeyArray *arr, unsigned int size);
extern TurkeyVariable turkey_array_get_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index);
extern void turkey_array_set_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable);
extern void turkey_array_remove(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count);
extern TurkeyArray *turkey_array_splice(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count);
extern void turkey_array_insert(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable);

/* buffer.h */
extern TurkeyBuffer *turkey_buffer_new(TurkeyVM *vm, size_t size);
extern TurkeyBuffer *turkey_buffer_new_native(TurkeyVM *vm, void *ptr, size_t size);
extern void turkey_buffer_delete(TurkeyVM *vm, TurkeyBuffer *buffer);
extern void turkey_buffer_dispose(TurkeyVM *vm, TurkeyBuffer *buffer);
extern void turkey_buffer_resize(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int size);
extern void turkey_buffer_write_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val);
extern void turkey_buffer_write_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val);
extern void turkey_buffer_write_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val);
extern void turkey_buffer_write_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val);
extern void turkey_buffer_write_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val);
extern void turkey_buffer_write_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val);
extern void turkey_buffer_write_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val);
extern void turkey_buffer_write_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val);
extern void turkey_buffer_write_float_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val);
extern void turkey_buffer_write_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val);
extern void turkey_buffer_write_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val);
extern unsigned long long int turkey_buffer_read_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern unsigned long long int turkey_buffer_read_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern unsigned long long int turkey_buffer_read_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern unsigned long long int turkey_buffer_read_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern signed long long int turkey_buffer_read_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern signed long long int turkey_buffer_read_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern signed long long int turkey_buffer_read_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern signed long long int turkey_buffer_read_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern double turkey_buffer_read_float_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern double turkey_buffer_read_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);
extern double turkey_buffer_read_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address);

/* callstack.h */
extern void turkey_callstack_init(TurkeyCallStack &stack);
extern void turkey_callstack_cleanup(TurkeyCallStack &stack);
/* adds and returns the entry (to write into) */
extern TurkeyCallStackEntry *turkey_callstack_push(TurkeyCallStack &stack);
/* pops an element and returns the entry */
extern TurkeyCallStackEntry *turkey_callstack_pop(TurkeyCallStack &stack);

/* gc.h */
extern void turkey_gc_init(TurkeyGarbageCollector &collector);
extern void turkey_gc_cleanup(TurkeyGarbageCollector &collector);
extern void turkey_gc_register_string(TurkeyGarbageCollector &collector, TurkeyString *string);
extern void turkey_gc_register_buffer(TurkeyGarbageCollector &collector, TurkeyBuffer *buffer);
extern void turkey_gc_register_array(TurkeyGarbageCollector &collector, TurkeyArray *arr);
extern void turkey_gc_register_object(TurkeyGarbageCollector &collector, TurkeyObject *object);
extern void turkey_gc_register_function_pointer(TurkeyGarbageCollector &collector, TurkeyFunctionPointer *closure);

/* stack.h */
extern void turkey_stack_init(TurkeyStack &stack);
extern void turkey_stack_cleanup(TurkeyStack &stack);
extern void turkey_stack_push(TurkeyStack &stack, const TurkeyVariable &value);
extern void turkey_stack_pop_no_return(TurkeyStack &stack);
extern void turkey_stack_pop(TurkeyStack &stack, TurkeyVariable &value);
extern void turkey_get(TurkeyStack &stack, unsigned int position, TurkeyVariable &value);
extern void turkey_set(TurkeyStack &stack, unsigned int position, const TurkeyVariable &value);

/* stringtable.h */
extern void turkey_stringtable_init(TurkeyStringTable &table);
extern void turkey_stringtable_cleanup(TurkeyStringTable &table);
extern TurkeyString *turkey_stringtable_newstring(TurkeyStringTable &table, const char *string, unsigned int length);
extern void turkey_stringtable_resize(TurkeyStringTable &table);
extern void turkey_stringtable_removestring(TurkeyStringTable &table, TurkeyString *string);

#endif
