#include "turkey_internal.h"
#include "hooks.h"
#include "external/half/half.hpp"

TurkeyBuffer *turkey_buffer_new(TurkeyVM *vm, size_t size) {
	if(size == 0)
		size = 1;

	TurkeyBuffer *buffer = (TurkeyBuffer *)turkey_allocate_memory(sizeof TurkeyBuffer);

	buffer->disposed = false;
	buffer->native = false;
	buffer->ptr = turkey_allocate_memory(size);
	buffer->size = size;

	turkey_memory_clear(buffer->ptr, size);

	turkey_gc_register_buffer(vm->garbage_collector, buffer);
	return buffer;
}

TurkeyBuffer *turkey_buffer_new_native(TurkeyVM *vm, void *ptr, size_t size) {
	TurkeyBuffer *buffer = (TurkeyBuffer *)turkey_allocate_memory(sizeof TurkeyBuffer);
	
	buffer->disposed = false;
	buffer->native = true;
	buffer->ptr = ptr;
	buffer->size = size;

	turkey_gc_register_buffer(vm->garbage_collector, buffer);
	return buffer;
}

void turkey_buffer_delete(TurkeyVM *vm, TurkeyBuffer *buffer) {
	/* cannot release a native pointer */
	if(!buffer->native && !buffer->disposed)
		turkey_free_memory(buffer->ptr);
	
	turkey_free_memory(buffer);
}

void turkey_buffer_dispose(TurkeyVM *vm, TurkeyBuffer *buffer) {
	/* cannot release a native pointer */
	if(!buffer->native && !buffer->disposed) {
		turkey_free_memory(buffer->ptr);
		buffer->disposed = true;
	}		
}

void turkey_buffer_resize(TurkeyVM *vm, TurkeyBuffer *buffer, size_t size) {
	/* cannot release a native pointer */
	if(buffer->native)
		return;

	if(size == 0)
		size = 1;
	else if(size == buffer->size)
		return; /* no need to resize */

	if(buffer->disposed) {
		buffer->ptr = turkey_allocate_memory(size);
		buffer->disposed = false;
		turkey_memory_clear(buffer->ptr, size);
	} else {
		buffer->ptr = turkey_reallocate_memory(buffer->ptr, size);
		/* clear the end of memory if we've grown */
		if(size > buffer->size) {
			size_t difference = size - buffer->size;
			turkey_memory_clear((void *)((size_t)buffer->ptr + difference, difference);
		}
	}
	buffer->size = size;
}

void turkey_buffer_write_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	*(unsigned char *)((size_t)buffer->ptr + address) = (unsigned char)val;
}

void turkey_buffer_write_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	*(unsigned short *)((size_t)buffer->ptr + address) = (unsigned short)val;
}

void turkey_buffer_write_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(unsigned int *)((size_t)buffer->ptr + address) = (unsigned int)val;
}

void turkey_buffer_write_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, unsigned long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(unsigned long long int *)((size_t)buffer->ptr + address) = val;
}

void turkey_buffer_write_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	*(signed char *)((size_t)buffer->ptr + address) = (signed char)val;
}

void turkey_buffer_write_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	*(signed short *)((size_t)buffer->ptr + address) = (signed short)val;
}

void turkey_buffer_write_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(signed int *)((size_t)buffer->ptr + address) = (signed int)val;
}

void turkey_buffer_write_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(signed long long int *)((size_t)buffer->ptr + address) = val;
}

void turkey_buffer_write_float_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	*(half_float::half *)((size_t)buffer->ptr + address) = half_float::half((float)val);
}

void turkey_buffer_write_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(float *)((size_t)buffer->ptr + address) = (float)val;
}

void turkey_buffer_write_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address, double val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(double *)((size_t)buffer->ptr + address) = val;
}

unsigned long long int turkey_buffer_read_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	return (unsigned long long int)*(unsigned char *)((size_t)buffer->ptr + address);
}

unsigned long long int turkey_buffer_read_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	return (unsigned long long int)*(unsigned short *)((size_t)buffer->ptr + address);
}

unsigned long long int turkey_buffer_read_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	return (unsigned long long int)*(unsigned int *)((size_t)buffer->ptr + address);
}

unsigned long long int turkey_buffer_read_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	return *(unsigned long long int *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	return (signed long long int)*(signed char *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	return (signed long long int)*(signed short *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	return (signed long long int)*(signed int *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	return *(signed long long int *)((size_t)buffer->ptr + address);
}

double turkey_buffer_read_float_16(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	/* half only has a cast to float, so we need to cast it twice to double */
	return (double)((float)(*(half_float::half *)((size_t)buffer->ptr + address)));
}

double turkey_buffer_read_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	return (double)*(float *)((size_t)buffer->ptr + address);
}

double turkey_buffer_read_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, unsigned long long int address) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	return *(double *)((size_t)buffer->ptr + address);
}

