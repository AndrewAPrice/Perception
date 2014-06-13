/* header file for the Turkey virtual machine */
#ifndef TURKEY_H
#define TURKEY_H

#ifdef DEBUG
#include <assert.h>
#else
#define assert(v)
#endif

struct TurkeyVM;

struct TurkeyFunctionPointer;
struct TurkeyGarbageCollectedObject;

struct TurkeySettings {
	// Use debug settings? Can handle breakpoints, 
	bool debug;
	void *tag;
};

enum TurkeyType {
	TT_Boolean = 0,
	TT_Unsigned = 1,
	TT_Signed = 2,
	TT_Float = 3,
	TT_Null = 4, // all objects beyond null are garbage collected

	TT_Object = 5,
	TT_Array = 6,
	TT_Buffer = 7,
	TT_FunctionPointer = 8,
	TT_String = 9,
	TT_Closure = 10 // used internally
	// 11
	// 12
	// 13
	// 14
	// 15 - undefined
};

struct TurkeyString;
struct TurkeyArray;
struct TurkeyFunctionPointer;
struct TurkeyObject;
struct TurkeyBuffer;
struct TurkeyFunction;
struct TurkeyModule;

struct TurkeyVariable {
	TurkeyType type;
	union {
		double float_value;
		signed long long int signed_value;
		unsigned long long int unsigned_value;
		bool boolean_value;
		TurkeyString *string;
		TurkeyArray *array;
		TurkeyFunctionPointer *function;
		TurkeyObject *object;
		TurkeyBuffer *buffer;
	};
};

/* native functions */
typedef TurkeyVariable (*TurkeyNativeFunction)(TurkeyVM *vm, void *closure, unsigned int argc);

typedef void (*TurkeyInstructionHandler)(TurkeyVM *vm);

/****** Virtual Machines -
 TurkeyVMs are unique instances of the virtual machine, many can be running simultaniously.
 *******/

/* Initializes a Turkey VM */
extern TurkeyVM *turkey_init(TurkeySettings *settings);
/* Cleans up a Turkey VM instance. Before doing this unhold any objects you may be holding or else the garbage collector won't release them. */
extern void turkey_cleanup(TurkeyVM *vm);

/* Loads a file and runs its main function - pushes it's exports object */
extern void turkey_require(TurkeyVM *vm, unsigned int index);
extern void turkey_require(TurkeyVM *vm);

/* Registers an internal module. When Shovel/Turkey code calls require, it checks to see if there's an internal module to return first
   - if there is then 'obj' passed here is returned, otherwise it loads a physical file on disk. */
extern void turkey_register_module(TurkeyVM *vm, unsigned int ind_moduleName, unsigned int ind_obj);

/* gc.cpp */
/* invoke the garbage collector */
extern void turkey_gc_collect(TurkeyVM *vm);
/* Places a hold on an object - when saving references in native code - so garbage collector doesn't clean them up */
extern void turkey_gc_hold(TurkeyVM *vm, TurkeyVariable &variable);
extern void turkey_gc_hold(TurkeyVM *vm, TurkeyGarbageCollectedObject *obj, TurkeyType type);
extern void turkey_gc_unhold(TurkeyVM *vm, TurkeyVariable &variable);
extern void turkey_gc_unhold(TurkeyVM *vm, TurkeyGarbageCollectedObject *ob, TurkeyType typej);

/* interpreter.cpp */
extern TurkeyVariable turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, size_t argc);
extern void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, size_t argc);


/****** variables *******/
/* stack_helpers.cpp */
extern void turkey_push_string(TurkeyVM *vm, const char *string);
extern void turkey_push_string_l(TurkeyVM *vm, const char *string, unsigned int length);
extern void turkey_push_object(TurkeyVM *vm);
extern void turkey_push_buffer(TurkeyVM *vm, size_t size);
extern void turkey_push_buffer_wrapper(TurkeyVM *vm, size_t size, void *c);
extern void turkey_push_array(TurkeyVM *vm, size_t size);
extern void turkey_push_native_function(TurkeyVM *vm, TurkeyNativeFunction func, void *closure);
extern void turkey_push_boolean(TurkeyVM *vm, bool val);
extern void turkey_push_signed_integer(TurkeyVM *vm, signed long long int val);
extern void turkey_push_unsigned_integer(TurkeyVM *vm, unsigned long long int val);
extern void turkey_push_float(TurkeyVM *vm, double val);
extern void turkey_push_null(TurkeyVM *vm);
extern void turkey_push(TurkeyVM *vm, TurkeyVariable &variable);

extern void turkey_grab(TurkeyVM *vm, unsigned int index);
extern TurkeyVariable turkey_pop(TurkeyVM *vm);
extern void turkey_pop_no_return(TurkeyVM *vm);
extern void turkey_swap(TurkeyVM *vm, unsigned int ind1, unsigned int ind2);

extern TurkeyVariable turkey_get(TurkeyVM *vm, unsigned int index);
extern void turkey_set(TurkeyVM *vm, unsigned int index, TurkeyVariable &var);

/**** objects/arrays ****/
/* grabs an element and pushes it on the stack */
extern TurkeyVariable turkey_get_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key);
/* calls the Shovel equivilent of obj[key] = val; */
extern void turkey_set_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key, unsigned int ind_val);
extern void turkey_delete_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key);

/***** conversions *****/
/* conversions.cpp */
extern TurkeyString *turkey_to_string(TurkeyVM *vm, TurkeyVariable &var_in);
extern unsigned long long int turkey_to_unsigned(TurkeyVM *vm, TurkeyVariable &var_in);
extern long long int turkey_to_signed(TurkeyVM *vm, TurkeyVariable &var_in);
extern double turkey_to_float(TurkeyVM *vm, TurkeyVariable &var_in);
extern bool turkey_to_boolean(TurkeyVM *vm, TurkeyVariable &var_in);

/***** debugging ****/
/*
extern void turkey_debug_pause(TurkeyVM *vm);
extern void turkey_debug_continue(TurkeyVM *vm);
extern void turkey_add_breakpoint(TurkeyVM *vm, TurkeyObject *obj, unsigned int byte);
extern void turkey_remove_breakpoint(TurkeyVM *vm, unsigned int byte);
*/

#define TURKEY_IS_TYPE_NUMBER(type) (type <= 4)

struct TurkeyGarbageCollectedObject {
	bool marked; /* has this item been marked for sweeping? */
	unsigned int hold; /* don't garbage collect if true */
	TurkeyGarbageCollectedObject *gc_next;
	TurkeyGarbageCollectedObject *gc_prev;
};

struct TurkeyClosure: TurkeyGarbageCollectedObject {
	TurkeyClosure *parent; /* parent closure, look in here if it's not found in this closure */
	unsigned int count; /* number of variables */
	TurkeyVariable *variables; /* array of variables*/
};

struct TurkeyString : TurkeyGarbageCollectedObject {
	char *string;
	unsigned int length;
	unsigned int hash;
	TurkeyString *next; /* next string in a chain */
	TurkeyString *prev; /* previous string in a chain */
};

struct TurkeyStringTable {
	/* TurkeyStringTable is actually a hash table for storing strings in. */
	TurkeyString **strings; /* array of string pointers */
	unsigned int count; /* number of items in the string table */
	unsigned int length; /* length of the string table */

	/* some static strings, used in the VM */
	TurkeyString *s_true;
	TurkeyString *s_false;

	TurkeyString *s_boolean;
	TurkeyString *s_unsigned;
	TurkeyString *s_signed;
	TurkeyString *s_float;
	TurkeyString *s_null;
	TurkeyString *s_object;
	TurkeyString *s_array;
	TurkeyString *s_buffer;
	TurkeyString *s_function;
	TurkeyString *s_string;

	/* string symbols */
	TurkeyString *ss_blank;
	TurkeyString *ss_opening_bracket;
	TurkeyString *ss_closing_bracket;
	TurkeyString *ss_opening_brace;
	TurkeyString *ss_closing_brace;
	TurkeyString *ss_colon;
	TurkeyString *ss_comma;
	TurkeyString *ss_doublequote;

	TurkeyString *ss_add;
	TurkeyString *ss_subtract;
	TurkeyString *ss_divide;
	TurkeyString *ss_multiply;
	TurkeyString *ss_modulo;
	TurkeyString *ss_increment;
	TurkeyString *ss_decrement;
	TurkeyString *ss_xor;
	TurkeyString *ss_and;
	TurkeyString *ss_or;
	TurkeyString *ss_not;
	TurkeyString *ss_shift_left;
	TurkeyString *ss_shift_right;
	TurkeyString *ss_rotate_left;
	TurkeyString *ss_rotate_right;
	TurkeyString *ss_less_than;
	TurkeyString *ss_greater_than;
	TurkeyString *ss_less_than_or_equals;
	TurkeyString *ss_greater_than_or_equals;
};

struct TurkeyFunctionPointer : TurkeyGarbageCollectedObject {
	union {
		struct managed_t {
			TurkeyFunction *function; /* the function to call */
			TurkeyClosure *closure; /* the closure */
		} managed;
		struct native_t {
			TurkeyNativeFunction function;
			void *closure;
		} native;
	};
	bool is_native; /* is this a native function? */
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

struct TurkeyObjectProperty {
	TurkeyString *key; /* key identifying the property */
	TurkeyObjectProperty *next; /* pointer to the next property at this chain */
	TurkeyVariable value; /* the value */
};

struct TurkeyObject : TurkeyGarbageCollectedObject {
	unsigned int count; /* the number of properties */
	unsigned int size; /* size of the hash table */
	TurkeyObjectProperty **properties; /* hash table of properties */
};

struct TurkeyFunction {
	TurkeyModule *module; /* module this is from */
	void *start; /* pointer to the start of the bytecode for this function */
	void *end; /* pointer beyond the last bytecode for this function */
	unsigned int parameters; /* number of parameters this function expects */
	unsigned int closures; /* number of closure variables this function creates */
	unsigned int locals; /* number of local variables this function has */
};

struct TurkeyStack {
	unsigned int top; /* top of the stack for the current call frame */
	unsigned int position; /* current position */
	unsigned int length; /* length of the variables array */
	TurkeyVariable *variables; // array of variables
};

struct TurkeyFunctionArray {
	unsigned int count; /* number of functions */
	TurkeyFunction *functions; /* array of turkey functions */
};

struct TurkeyModule {
	TurkeyFunction **functions; /* array of function pointers (from this loaded module) */
	unsigned int function_count; /* number of functions this module has in the function table */
	
	void *code_block; /* pointer to the code block */
	unsigned int code_block_size; /* size of the code block */

	TurkeyString **strings; /* array of strings pointers (hardcoded into the loaded module) */
	unsigned int string_count; /* number of strings this module has in the string array */

	TurkeyModule *next; /* next module, linked list */
};

struct TurkeyLoadedModule {
	TurkeyString *Name; /* the name of the module */
	TurkeyVariable ReturnVariable; /* the variable to return when the module is 'require'd */
	TurkeyLoadedModule *Next; /* next module in the linked list */
};

struct TurkeyLoadedModules {
	TurkeyLoadedModule *external_modules; /* linked list of modules that were loaded via file */
	TurkeyLoadedModule *internal_modules; /* linked list of modules that were internally registered */
};

struct TurkeyGarbageCollector {
	 /* pointers to linked list of items to collect */
	TurkeyString *strings;
	TurkeyBuffer *buffers;
	TurkeyArray *arrays;
	TurkeyObject *objects;
	TurkeyFunctionPointer *function_pointers;
	TurkeyClosure *closures;

	unsigned long long int items; /* number of allocated items */
	#ifdef DEBUG
	/* items on hold are not collected by the gc, this can cause memory leaks if cleanup the GC without unholding all objects */
	size_t items_on_hold;
	#endif
};

struct TurkeyInterpreterState {
	TurkeyInterpreterState *parent; /* parent state */
	// unsigned int callee_local_stack_top; /* top of the local stack */
	// unsigned int callee_variable_stack_top; /* top of the variable stack */
	// unsigned int callee_parameter_stack_top; /* top of the parameter stack */
	TurkeyClosure *closure; /* the current closure */
	TurkeyFunction *function; /* the current function */
	bool executing; /* continue executing? return sets this to false */
	size_t code_ptr; /* code pointer */
	size_t code_start; /* start of the current code block */
	size_t code_end; /* end of the current code block */
};

struct TurkeyVM {
	bool debug; /* no jit */
	void *tag; /* tag when running multiple instances of the VM */

	TurkeyModule *modules; /* loaded modules */
	TurkeyStringTable string_table; /* table of strings */
	TurkeyFunctionArray function_array; /* array of functions */
	TurkeyStack variable_stack; /* the variable stack, for operations */
	TurkeyStack local_stack; /* the local stack, that functions store variables into */
	// TurkeyStack parameter_stack; /* stack of parameters */
	TurkeyGarbageCollector garbage_collector;
	TurkeyLoadedModules loaded_modules; /* loaded modules and their files */

	TurkeyInterpreterState *interpreter_state; /* pointer to the current interpreter state */
};

/* array.cpp */
extern TurkeyArray *turkey_array_new(TurkeyVM *vm, unsigned int size);
extern TurkeyArray *turkey_array_append(TurkeyVM *vm, TurkeyArray *a, TurkeyArray *b);
extern void turkey_array_delete(TurkeyVM *vm, TurkeyArray *arr);
extern void turkey_array_push(TurkeyVM *vm, TurkeyArray *arr, const TurkeyVariable &variable);
extern void turkey_array_grow(TurkeyVM *vm, TurkeyArray *arr);
extern void turkey_array_allocate(TurkeyVM *vm, TurkeyArray *arr, unsigned int size);
extern TurkeyVariable turkey_array_get_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index);
extern void turkey_array_set_element(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable);
extern void turkey_array_remove(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count);
extern TurkeyArray *turkey_array_splice(TurkeyVM *vm, TurkeyArray *arr, unsigned int start, unsigned int count);
extern void turkey_array_insert(TurkeyVM *vm, TurkeyArray *arr, unsigned int index, const TurkeyVariable &variable);

/* buffer.cpp */
extern TurkeyBuffer *turkey_buffer_new(TurkeyVM *vm, size_t size);
extern TurkeyBuffer *turkey_buffer_new_native(TurkeyVM *vm, void *ptr, size_t size);
extern TurkeyBuffer *turkey_buffer_append(TurkeyVM *vm, TurkeyBuffer *a, TurkeyBuffer *b);
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

/* callstack.cpp */
/*extern void turkey_callstack_init(TurkeyCallStack &stack);
extern void turkey_callstack_cleanup(TurkeyCallStack &stack);
extern TurkeyCallStackEntry *turkey_callstack_push(TurkeyCallStack &stack);
extern TurkeyCallStackEntry *turkey_callstack_pop(TurkeyCallStack &stack);
extern void turkey_stack_pop_no_return(TurkeyStack &stack);
*/


/* closure.cpp */
extern TurkeyClosure *turkey_closure_create(TurkeyVM *vm, TurkeyClosure *parent, unsigned int variables);
extern void turkey_closure_delete(TurkeyVM *vm, TurkeyClosure *closure);
extern void turkey_closure_get(TurkeyVM *vm, unsigned int position, TurkeyVariable &value);
extern void turkey_closure_set(TurkeyVM *vm, unsigned int position, const TurkeyVariable &value);

/* functionpointer.cpp */
extern TurkeyFunctionPointer *turkey_functionpointer_new(TurkeyVM *vm, TurkeyFunction *function, TurkeyClosure *closure);
extern TurkeyFunctionPointer *turkey_functionpointer_new_native(TurkeyVM *vm, TurkeyNativeFunction function, void *closure);
extern void turkey_functionpointer_delete(TurkeyVM *vm, TurkeyFunctionPointer *funcptr);

/* gc.cpp */
extern void turkey_gc_init(TurkeyVM *vm);
extern void turkey_gc_cleanup(TurkeyVM *vm);
extern void turkey_gc_register_string(TurkeyGarbageCollector &collector, TurkeyString *string);
extern void turkey_gc_register_buffer(TurkeyGarbageCollector &collector, TurkeyBuffer *buffer);
extern void turkey_gc_register_array(TurkeyGarbageCollector &collector, TurkeyArray *arr);
extern void turkey_gc_register_object(TurkeyGarbageCollector &collector, TurkeyObject *object);
extern void turkey_gc_register_function_pointer(TurkeyGarbageCollector &collector, TurkeyFunctionPointer *function_pointer);
extern void turkey_gc_register_closure(TurkeyGarbageCollector &collector, TurkeyClosure *closure);

/* interpreter.cpp */
extern void turkey_interpreter_init(TurkeyVM *vm);
extern void turkey_interpreter_cleanup(TurkeyVM *vm);

/* module.cpp */
extern void turkey_module_init(TurkeyVM *vm);
extern void turkey_module_cleanup(TurkeyVM *vm);
extern TurkeyVariable turkey_module_load_file(TurkeyVM *vm, TurkeyString *filepath);

/* object.cpp */
extern TurkeyObject *turkey_object_new(TurkeyVM *vm);
extern void turkey_object_delete(TurkeyVM *vm, TurkeyObject *object);
extern TurkeyVariable turkey_object_get_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name);
extern void turkey_object_set_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name, const TurkeyVariable &value);
extern void turkey_object_grow(TurkeyVM *vm, TurkeyObject *object);
extern void turkey_object_delete_property(TurkeyVM *vm, TurkeyObject *object, TurkeyString *name);
/* calls an operator and pushes the return value onto the stack */
extern void turkey_object_call_operator(TurkeyVM *vm, TurkeyObject *object, TurkeyString *oper, TurkeyVariable &operand);
extern void turkey_object_call_operator(TurkeyVM *vm, TurkeyObject *object, TurkeyString *oper);

/* stack.cpp */
extern void turkey_stack_init(TurkeyVM *vm, TurkeyStack &stack);
extern void turkey_stack_cleanup(TurkeyVM *vm, TurkeyStack &stack);
extern void turkey_stack_push(TurkeyVM *vm, TurkeyStack &stack, const TurkeyVariable &value);
extern void turkey_stack_pop_no_return(TurkeyStack &stack);
extern void turkey_stack_pop(TurkeyStack &stack, TurkeyVariable &value);
extern void turkey_stack_get(TurkeyStack &stack, unsigned int position, TurkeyVariable &value);
extern void turkey_stack_set(TurkeyStack &stack, unsigned int position, const TurkeyVariable &value);

/* string.cpp */
/* escapes a string and wrap it in quotes */
extern TurkeyString *turkey_string_escape(TurkeyVM *vm, TurkeyString *str);
/* appends one string to another */
extern TurkeyString *turkey_string_append(TurkeyVM *vm, TurkeyString *stra, TurkeyString *strb);
/* returns the substring of a string */
extern TurkeyString *turkey_string_substring(TurkeyVM *vm, TurkeyString *str, unsigned int start, unsigned int length);

/* stringtable.cpp */
extern void turkey_stringtable_init(TurkeyVM *vm);
extern void turkey_stringtable_cleanup(TurkeyVM *vm);
extern TurkeyString *turkey_stringtable_newstring(TurkeyVM *vm, const char *string, unsigned int length);
extern void turkey_stringtable_grow(TurkeyVM *vm);
extern void turkey_stringtable_removestring(TurkeyVM *vm, TurkeyString *string);

/* instructions.cpp */
extern TurkeyInstructionHandler turkey_interpreter_operations[256];

/* utilities */
/* fast hash if size is a power of two */
inline unsigned int fast_mod(unsigned int hash, unsigned int size) {
	/* size be a power of two */
	assert((size & (size - 1)) == 0);
	return hash & (size - 1);
}

#endif
