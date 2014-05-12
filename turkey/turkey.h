/* header file for the Turkey virtual machine */
#ifndef TURKEY_H
#define TURKEY_H

struct TurkeyVM;

struct TurkeyObject {
	unsigned char params;
	unsigned int value;
};

struct TurkeyFunctionPointer;

struct TurkeySettings {
	// Use debug settings? Can handle breakpoints, 
	bool debug;
};

enum TurkeyType {
	TT_Null = 0,
	TT_Boolean = 1,
	TT_Unsigned = 2,
	TT_Signed = 3,
	TT_Float = 4,
	TT_Object = 5,
	TT_Array = 6,
	TT_Buffer = 7,
	TT_FunctionPointer = 8,
	TT_String = 9,
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

struct TurkeyVariable {
	TurkeyType type;
	union {
		double float_value;
		signed long long int signed_value;
		unsigned long long int unsigned_value;
		char boolean_value;
		TurkeyString *string;
		TurkeyArray *array;
		TurkeyFunctionPointer *function;
		TurkeyObject *object;
		TurkeyBuffer *buffer;
	};
};

/* native functions */
typedef unsigned int (*TurkeyNativeFunction)(TurkeyVM *vm, void *closure, unsigned int argc);

/****** Virtual Machines -
 TurkeyVMs are unique instances of the virtual machine, many can be running simultaniously.
*******/

/* Initializes a Turkey VM */
extern TurkeyVM *turkey_init(TurkeySettings *settings);
/* Cleans up a Turkey VM instance. Before doing this unhold any objects you may be holding or else the garbage collector won't release them. */
extern void turkey_cleanup(TurkeyVM *vm);

/* Loads a file and runs its main function - pushes it's exports object */
extern void turkey_require(TurkeyVM *vm, unsigned int inded);

/* Registers an internal module. When Shovel/Turkey code calls require, it checks to see if there's an internal module to return first
   - if there is then 'obj' passed here is returned, otherwise it loads a physical file on disk. */
extern void turkey_register_module(TurkeyVM *vm, unsigned int ind_moduleName, unsigned int ind_obj);

/* invoke the garbage collector */
extern void turkey_gc_collect(TurkeyVM *vm);
/* Places a hold on an object - when saving references in native code - so garbage collector doesn't clean them up */
extern void turkey_gc_hold(TurkeyVM *vm, TurkeyVariable &variable);
extern void turkey_gc_unhold(TurkeyVM *vm, TurkeyVariable &variable);


/****** variables *******/
extern void turkey_push_string(TurkeyVM *vm, const char *string);
extern void turkey_push_string_l(TurkeyVM *vm, const char *string, unsigned int length);
extern void turkey_push_object(TurkeyVM *vm);
extern void turkey_push_buffer(TurkeyVM *vm, size_t size);
extern void turkey_push_buffer_wrapper(TurkeyVM *vm, size_t size, void *c);
extern void turkey_push_array(TurkeyVM *vm, size_t size);
extern void turkey_push_native_function(TurkeyVM *vm, TurkeyNativeFunction func, void *closure);
extern void turkey_push_function(TurkeyVM *vm, TurkeyFunctionPointer *func);
extern void turkey_push_boolean(TurkeyVM *vm, bool val);
extern void turkey_push_signed_integer(TurkeyVM *vm, signed long long int val);
extern void turkey_push_unsigned_integer(TurkeyVM *vm, unsigned long long int val);
extern void turkey_push_float(TurkeyVM *vm, double val);
extern void turkey_push_null();

extern void turkey_grab(TurkeyVM *vm, unsigned int index);
extern void turkey_pop(TurkeyVM *vm);
extern void turkey_swap(TurkeyVM *vm, unsigned int ind1, unsigned int ind2);

extern size_t turkey_get_type(TurkeyVM *vm, unsigned int index);

/* Extract fields out of variables */
extern double turkey_get_double(TurkeyVM *vm, unsigned int index);
extern bool turkey_get_bool(TurkeyVM *vm, unsigned int index);
extern unsigned long long int turkey_get_unsigned_integer(TurkeyVM *vm, unsigned int index);
extern signed long long int turkey_get_signed_integer(TurkeyVM *vm, unsigned int index);
extern const char *turkey_get_string(TurkeyVM *vm, unsigned int index); /* if you pop this off the stack and the GC is called, then your char* may be dangle */
extern TurkeyFunctionPointer *turkey_get_function(TurkeyVM *vm, unsigned int index);


/***** functions *****/
extern void turkey_call_function(TurkeyVM *vm, TurkeyFunctionPointer *func, size_t argc);

/**** objects/arrays ****/
/* grabs an element and pushes it on the stack */
extern void turkey_get_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key);
/* calls the Shovel equivilent of obj[key] = val; */
extern void turkey_set_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key, unsigned int ind_val);
extern void turkey_delete_element(TurkeyVM *vm, unsigned int ind_obj, unsigned int ind_key);

/**** buffers ****/


/***** debugging ****/
/*
extern void turkey_debug_pause(TurkeyVM *vm);
extern void turkey_debug_continue(TurkeyVM *vm);
extern void turkey_add_breakpoint(TurkeyVM *vm, TurkeyObject *obj, unsigned int byte);
extern void turkey_remove_breakpoint(TurkeyVM *vm, unsigned int byte);
*/

#endif
