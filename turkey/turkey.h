/* header file for the Turkey virtual machine */
#ifndef TURKEY_H
#define TURKEY_H

/* define PRINT_SSA to output the internal SSA form after loading a module - you should only need this is working on Turkey's code */
#define PRINT_SSA

/* define OPTIMIZE_SSA to optimize the SSA form  - you should only need this is working on Turkey's code */
#define OPTIMIZE_SSA

#ifdef DEBUG
#include <assert.h>
#else
#define assert(v)
#endif

/* byte-code instructions */
#define turkey_instruction_add 0
#define turkey_instruction_subtract 1
#define turkey_instruction_divide 2
#define turkey_instruction_multiply 3
#define turkey_instruction_modulo 4
#define turkey_instruction_increment 5
#define turkey_instruction_decrement 6
#define turkey_instruction_xor 7
#define turkey_instruction_and 8
#define turkey_instruction_or 9
#define turkey_instruction_not 10
#define turkey_instruction_shift_left 11
#define turkey_instruction_shift_right 12
#define turkey_instruction_rotate_left 13
#define turkey_instruction_rotate_right 14
#define turkey_instruction_is_null 15
#define turkey_instruction_is_not_null 16
#define turkey_instruction_equals 17
#define turkey_instruction_not_equals 18
#define turkey_instruction_less_than 19
#define turkey_instruction_greater_than 20
#define turkey_instruction_less_than_or_equals 21
#define turkey_instruction_greater_than_or_equals 22
#define turkey_instruction_is_true 23
#define turkey_instruction_is_false 24
#define turkey_instruction_pop 25
#define turkey_instruction_pop_many 26
#define turkey_instruction_grab_8 27
#define turkey_instruction_grab_16 28
#define turkey_instruction_grab_32 29
#define turkey_instruction_push_many_nulls 30

#define turkey_instruction_store_8 33
#define turkey_instruction_store_16 34
#define turkey_instruction_store_32 35
#define turkey_instruction_swap_8 36
#define turkey_instruction_swap_16 37
#define turkey_instruction_swap_32 38
#define turkey_instruction_load_closure_8 39
#define turkey_instruction_load_closure_16 40
#define turkey_instruction_load_closure_32 41
#define turkey_instruction_store_closure_8 42
#define turkey_instruction_store_closure_16 43
#define turkey_instruction_store_closure_32 44
#define turkey_instruction_new_array 45
#define turkey_instruction_load_element 46
#define turkey_instruction_save_element 47
#define turkey_instruction_new_object 48
#define turkey_instruction_delete_element 49
#define turkey_instruction_new_buffer 50
#define turkey_instruction_load_buffer_unsigned_8 51
#define turkey_instruction_load_buffer_unsigned_16 52
#define turkey_instruction_load_buffer_unsigned_32 53
#define turkey_instruction_load_buffer_unsigned_64 54
#define turkey_instruction_store_buffer_unsigned_8 55
#define turkey_instruction_store_buffer_unsigned_16 56
#define turkey_instruction_store_buffer_unsigned_32 57
#define turkey_instruction_store_buffer_unsigned_64 58
#define turkey_instruction_load_buffer_signed_8 59
#define turkey_instruction_load_buffer_signed_16 60
#define turkey_instruction_load_buffer_signed_32 61
#define turkey_instruction_load_buffer_signed_64 62
#define turkey_instruction_store_buffer_signed_8 63
#define turkey_instruction_store_buffer_signed_16 64
#define turkey_instruction_store_buffer_signed_32 65
#define turkey_instruction_store_buffer_signed_64 66

#define turkey_instruction_load_buffer_float_32 68
#define turkey_instruction_load_buffer_float_64 69

#define turkey_instruction_store_buffer_float_32 71
#define turkey_instruction_store_buffer_float_64 72
#define turkey_instruction_push_integer_8 73
#define turkey_instruction_push_integer_16 74
#define turkey_instruction_push_integer_32 75
#define turkey_instruction_push_integer_64 76
#define turkey_instruction_to_integer 77
#define turkey_instruction_push_unsigned_integer_8 78
#define turkey_instruction_push_unsigned_integer_16 79
#define turkey_instruction_push_unsigned_integer_32 80
#define turkey_instruction_push_unsigned_integer_64 81
#define turkey_instruction_to_unsigned_integer 82
#define turkey_instruction_push_float 83
#define turkey_instruction_to_float 84
#define turkey_instruction_push_true 85
#define turkey_instruction_push_false 86
#define turkey_instruction_push_null 87
#define turkey_instruction_push_string_8 88
#define turkey_instruction_push_string_16 89
#define turkey_instruction_push_string_32 90
#define turkey_instruction_push_function 91
#define turkey_instruction_call_function_8 92
#define turkey_instruction_call_function_16 93
#define turkey_instruction_call_function_no_return_8 94
#define turkey_instruction_call_function_no_return_16 95
#define turkey_instruction_return_null 96
#define turkey_instruction_return 97
#define turkey_instruction_get_type 98
#define turkey_instruction_jump_8 99
#define turkey_instruction_jump_16 100
#define turkey_instruction_jump_32 101
#define turkey_instruction_jump_if_true_8 102
#define turkey_instruction_jump_if_true_16 103
#define turkey_instruction_jump_if_true_32 104
#define turkey_instruction_jump_if_false_8 105
#define turkey_instruction_jump_if_false_16 106
#define turkey_instruction_jump_if_false_32 107
#define turkey_instruction_jump_if_null_8 108
#define turkey_instruction_jump_if_null_16 109
#define turkey_instruction_jump_if_null_32 110
#define turkey_instruction_jump_if_not_null_8 111
#define turkey_instruction_jump_if_not_null_16 112
#define turkey_instruction_jump_if_not_null_32 113
#define turkey_instruction_require 114

#define turkey_instruction_to_string 121
#define turkey_instruction_invert 122
#define turkey_instruction_call_procedure_8 123
#define turkey_instruction_call_procedure_16 124

/* ssa immediate representation instructions */
#define turkey_ir_add 0
#define turkey_ir_subtract 1
#define turkey_ir_divide 2
#define turkey_ir_multiply 3
#define turkey_ir_modulo 4
#define turkey_ir_invert 5
#define turkey_ir_increment 6
#define turkey_ir_decrement 7
#define turkey_ir_xor 8
#define turkey_ir_and 9
#define turkey_ir_or 10
#define turkey_ir_not 11
#define turkey_ir_shift_left 12
#define turkey_ir_shift_right 13
#define turkey_ir_rotate_left 14
#define turkey_ir_rotate_right 15
#define turkey_ir_is_null 16
#define turkey_ir_is_not_null 17
#define turkey_ir_equals 18
#define turkey_ir_not_equals 19
#define turkey_ir_less_than 20
#define turkey_ir_greater_than 21
#define turkey_ir_less_than_or_equals 22
#define turkey_ir_greater_than_or_equals 23
#define turkey_ir_is_true 24
#define turkey_ir_is_false 25
#define turkey_ir_parameter 26
#define turkey_ir_load_closure 27
#define turkey_ir_store_closure 28
#define turkey_ir_new_array 29
#define turkey_ir_load_element 30
#define turkey_ir_save_element 31
#define turkey_ir_new_object 32
#define turkey_ir_delete_element 33
#define turkey_ir_new_buffer 34
#define turkey_ir_load_buffer_unsigned_8 35
#define turkey_ir_load_buffer_unsigned_16 36
#define turkey_ir_load_buffer_unsigned_32 37
#define turkey_ir_load_buffer_unsigned_64 38
#define turkey_ir_store_buffer_unsigned_8 39
#define turkey_ir_store_buffer_unsigned_16 40
#define turkey_ir_store_buffer_unsigned_32 41
#define turkey_ir_store_buffer_unsigned_64 42
#define turkey_ir_load_buffer_signed_8 43
#define turkey_ir_load_buffer_signed_16 44
#define turkey_ir_load_buffer_signed_32 45
#define turkey_ir_load_buffer_signed_64 46
#define turkey_ir_store_buffer_signed_8 47
#define turkey_ir_store_buffer_signed_16 48
#define turkey_ir_store_buffer_signed_32 49
#define turkey_ir_store_buffer_signed_64 50
#define turkey_ir_load_buffer_float_32 51
#define turkey_ir_load_buffer_float_64 52
#define turkey_ir_store_buffer_float_32 53
#define turkey_ir_store_buffer_float_64 54
#define turkey_ir_signed_integer 55
#define turkey_ir_to_signed_integer 56
#define turkey_ir_unsigned_integer 57
#define turkey_ir_to_unsigned_integer 58
#define turkey_ir_float 59
#define turkey_ir_to_float 60
#define turkey_ir_true 61
#define turkey_ir_false 62
#define turkey_ir_null 63
#define turkey_ir_string 64
#define turkey_ir_to_string 65
#define turkey_ir_function 66
#define turkey_ir_call_function 67
#define turkey_ir_call_function_no_return 68
#define turkey_ir_call_pure_function 69
#define turkey_ir_return_null 70
#define turkey_ir_return 71
#define turkey_ir_push 72
#define turkey_ir_get_type 73
#define turkey_ir_jump 74
#define turkey_ir_jump_if_true 75
#define turkey_ir_jump_if_false 76
#define turkey_ir_jump_if_null 77
#define turkey_ir_jump_if_not_null 78
#define turkey_ir_require 79

struct TurkeyVM;

#include "stack.h"

struct TurkeyFunctionPointer;
struct TurkeyGarbageCollectedObject;

struct TurkeySettings {
	// Use debug settings? Can handle breakpoints, 
	bool debug;
	void *tag;
};

enum TurkeyType : unsigned char {
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
	TT_Closure = 10, // used internally
	// 11
	// 12
	// 13
	TT_Unknown = 15,

	TT_Mask = 15, // for unmasking TT_Marked
	TT_Placeholder = 1 << 6, // for parameters that are placeholders (the caller branches and puts a parameter there) but not needed
	TT_Marked = 1 << 7 // used by the ssa optimizer
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


/* instruction in SSA form, used by the JIT compiler */
struct TurkeyInstruction {
	unsigned char instruction;
	unsigned char return_type;
	union {
		struct {
			unsigned int a;
			unsigned int b;
		};
		unsigned long long int large;
	};
};

struct TurkeyBasicBlock {
	unsigned int parameters_entering; /* number of parameters entering */
	TurkeyInstruction *instructions; /* array of ssa instructions */
	unsigned int instructions_count; /* number of instructions */

	unsigned int *entry_points; /* array of BB indicies control could have come from */
	unsigned int entry_point_count; /* size of the entry_point array */

	/* todo - add versioning info here */
};

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
extern TurkeyVariable turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc);
extern void turkey_call_function_no_return(TurkeyVM *vm, TurkeyFunctionPointer *funcptr, unsigned int argc);


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

/* ssa_conversions.cpp */
extern TurkeyString *turkey_ssa_to_string(TurkeyVM *vm, TurkeyInstruction &instruction);
extern unsigned long long int turkey_ssa_to_unsigned(TurkeyVM *vm, TurkeyInstruction &instruction);
extern long long int turkey_ssa_to_signed(TurkeyVM *vm, TurkeyInstruction &instruction);
extern double turkey_ssa_to_float(TurkeyVM *vm, TurkeyInstruction &instruction);
extern bool turkey_ssa_to_boolean(TurkeyVM *vm, TurkeyInstruction &instruction);

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
	TurkeyBasicBlock *basic_blocks; /* array of basic blocks */
	unsigned int basic_blocks_count; /* number of basic blocks */
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
	TurkeyStack<TurkeyVariable> variable_stack; /* the variable stack, for operations */
	// TurkeyStack local_stack; /* the local stack, that functions store variables into */
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

/* ssa.cpp */
extern void turkey_ssa_compile_function(TurkeyVM *vm, TurkeyFunction *function);

/* ssa_printer.cpp */
extern void turkey_ssa_printer_print_function(TurkeyVM *vm, TurkeyFunction *function);

/* ssa_optimizer.cpp */
extern bool turkey_ssa_optimizer_is_constant(const TurkeyInstruction &instruction);
extern bool turkey_ssa_optimizer_is_constant_number(const TurkeyInstruction &instruction);
extern bool turkey_ssa_optimizer_is_constant_string(const TurkeyInstruction &instruction);
extern void turkey_ssa_optimizer_touch_instruction(TurkeyVM *vm, TurkeyFunction *function, unsigned int bb, unsigned int inst);
extern void turkey_ssa_optimizer_mark_roots(TurkeyVM *vm, TurkeyFunction *function);
extern void turkey_ssa_optimizer_shrink(TurkeyVM *vm, TurkeyFunction *function);
extern void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function);

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
