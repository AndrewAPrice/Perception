#include "turkey.h"
#include "hooks.h"

TurkeyBuffer *turkey_buffer_new(TurkeyVM *vm, size_t size) {
	if(size == 0)
		size = 1;

	TurkeyBuffer *buffer = (TurkeyBuffer *)turkey_allocate_memory(vm->tag, sizeof TurkeyBuffer);

	buffer->disposed = false;
	buffer->native = false;
	buffer->ptr = turkey_allocate_memory(vm->tag, size);
	buffer->size = size;

	turkey_memory_clear(buffer->ptr, size);
	
	/* register with the gc */
	turkey_gc_register_buffer(vm->garbage_collector, buffer);
	return buffer;
}

TurkeyBuffer *turkey_buffer_new_native(TurkeyVM *vm, void *ptr, size_t size) {
	TurkeyBuffer *buffer = (TurkeyBuffer *)turkey_allocate_memory(vm->tag, sizeof TurkeyBuffer);
	
	buffer->disposed = false;
	buffer->native = true;
	buffer->ptr = ptr;
	buffer->size = size;
	
	/* register with the gc */
	turkey_gc_register_buffer(vm->garbage_collector, buffer);
	return buffer;
}

TurkeyBuffer *turkey_buffer_append(TurkeyVM *vm, TurkeyBuffer *a, TurkeyBuffer *b) {
	turkey_gc_hold(vm, a, TT_Buffer);
	turkey_gc_hold(vm, b, TT_Buffer);

	TurkeyBuffer *buffer = (TurkeyBuffer *)turkey_allocate_memory(vm->tag, sizeof TurkeyBuffer);

	size_t size = a->size + b->size;

	buffer->disposed = false;
	buffer->native = false;
	buffer->ptr = turkey_allocate_memory(vm->tag, size);
	buffer->size = size;

	turkey_memory_copy(buffer->ptr, a->ptr, a->size);
	turkey_memory_copy((void *)((size_t)buffer->ptr + a->size), b->ptr, b->size);

	turkey_gc_unhold(vm, a, TT_Buffer);
	turkey_gc_unhold(vm, b, TT_Buffer);
	
	/* register with the gc */
	turkey_gc_register_buffer(vm->garbage_collector, buffer);

	return buffer;
}

void turkey_buffer_delete(TurkeyVM *vm, TurkeyBuffer *buffer) {
	/* cannot release a native pointer */
	if(!buffer->native && !buffer->disposed)
		turkey_free_memory(vm->tag, buffer->ptr, buffer->size);
	
	turkey_free_memory(vm->tag, buffer, sizeof TurkeyBuffer);
}

void turkey_buffer_dispose(TurkeyVM *vm, TurkeyBuffer *buffer) {
	/* cannot release a native pointer */
	if(!buffer->native && !buffer->disposed) {
		turkey_free_memory(vm->tag, buffer->ptr, buffer->size);
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
		buffer->ptr = turkey_allocate_memory(vm->tag, size);
		buffer->disposed = false;
		turkey_memory_clear(buffer->ptr, size);
	} else {
		buffer->ptr = turkey_reallocate_memory(vm->tag, buffer->ptr, buffer->size, size);
		/* clear the end of memory if we've grown */
		if(size > buffer->size) {
			size_t difference = size - buffer->size;
			turkey_memory_clear((void *)((size_t)buffer->ptr + difference), difference);
		}
	}
	buffer->size = size;
}

void turkey_buffer_write_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, uint64_t val) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	*(unsigned char *)((size_t)buffer->ptr + address) = (unsigned char)val;
}

void turkey_buffer_write_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, uint64_t val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	*(unsigned short *)((size_t)buffer->ptr + address) = (unsigned short)val;
}

void turkey_buffer_write_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, uint64_t val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(unsigned int *)((size_t)buffer->ptr + address) = (unsigned int)val;
}

void turkey_buffer_write_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, uint64_t val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(uint64_t *)((size_t)buffer->ptr + address) = val;
}

void turkey_buffer_write_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size) return;

	*(signed char *)((size_t)buffer->ptr + address) = (signed char)val;
}

void turkey_buffer_write_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 1) return;

	*(signed short *)((size_t)buffer->ptr + address) = (signed short)val;
}

void turkey_buffer_write_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(signed int *)((size_t)buffer->ptr + address) = (signed int)val;
}

void turkey_buffer_write_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, signed long long int val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(signed long long int *)((size_t)buffer->ptr + address) = val;
}

void turkey_buffer_write_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, double val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 3) return;

	*(float *)((size_t)buffer->ptr + address) = (float)val;
}

void turkey_buffer_write_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address, double val) {
	if(buffer->disposed) return;
	if(address >= buffer->size - 7) return;

	*(double *)((size_t)buffer->ptr + address) = val;
}

uint64_t turkey_buffer_read_unsigned_8(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size) return 0;

	return (uint64_t)*(unsigned char *)((size_t)buffer->ptr + address);
}

uint64_t turkey_buffer_read_unsigned_16(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 1) return 0;

	return (uint64_t)*(unsigned short *)((size_t)buffer->ptr + address);
}

uint64_t turkey_buffer_read_unsigned_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 3) return 0;

	return (uint64_t)*(unsigned int *)((size_t)buffer->ptr + address);
}

uint64_t turkey_buffer_read_unsigned_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 7) return 0;

	return *(uint64_t *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_8(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size) return 0;

	return (signed long long int)*(signed char *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_16(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 1) return 0;

	return (signed long long int)*(signed short *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 3) return 0;

	return (signed long long int)*(signed int *)((size_t)buffer->ptr + address);
}

signed long long int turkey_buffer_read_signed_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 7) return 0;

	return *(signed long long int *)((size_t)buffer->ptr + address);
}


double turkey_buffer_read_float_32(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 3) return 0;

	return (double)*(float *)((size_t)buffer->ptr + address);
}

double turkey_buffer_read_float_64(TurkeyVM *vm, TurkeyBuffer *buffer, uint64_t address) {
	if(buffer->disposed) return 0;
	if(address >= buffer->size - 7) return 0;

	return *(double *)((size_t)buffer->ptr + address);
}

