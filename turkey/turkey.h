/* header file for the Turkey virtual machine */
#ifndef TURKEY_H
#define TURKEY_H

struct TurkeyVM;

struct TurkeyFunctionPointer;
struct TurkeyGarbageCollectedObject;

struct TurkeySettings {
	// Use debug settings? Can handle breakpoints, 
	bool debug;
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

#endif
