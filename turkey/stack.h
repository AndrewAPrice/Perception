#ifndef TURKEY_STACK_H
#define TURKEY_STACK_H
#include "hooks.h"

template<class T> class TurkeyStack {
public:
	TurkeyStack(void *tag) {
		Init(tag);
	}

	/* seperated init so it can be called explicitly when turkey_vm is created*/
	void Init(void *tag) {
		this->tag = tag;
		top = 0;
		position = 0;
		length = 16; /* initial stack size */
		variables = (T *)turkey_allocate_memory(tag, sizeof(T) * length);
	}

	~TurkeyStack() {
		turkey_free_memory(tag, variables, sizeof(T) * length);
	}

	void Push(const T &value) {
		if(length == position) {
			/* not enough room to push a variable on */
			unsigned int newLength = length * 2;

			variables = (T *)turkey_reallocate_memory(tag, variables,
				sizeof(T) * length, sizeof(T) * newLength);
			length = newLength;
		}

		variables[position] = value;
		position++;
	}

	void PopNoReturn() {
		if(position > top)
			position--;
	}

	bool Pop(T &value) {
		if(position == top)
			return false;

		position--;
		value = variables[position];
		return true;
	}

	/* 0 is at the bottom, indicies are backwards */
	bool Get(unsigned int pos, T &value) {
		if(pos >= (position - top))
			return false;

		value = variables[position - pos - 1];
		return true;
	}

	void Set(unsigned int pos, T &value) {
		if(pos >= (position - top))
			return;

		variables[position - pos - 1] = value;
	}

	/* empties out the stack */
	void Clear() {
		top = 0; position = 0;
	}

	/* remove at a position (from the start) and scoots everything down */
	void RemoveAtFromStart(unsigned int pos) {
		if(pos >= position)
			return;

		if(pos < top)
			top--;

		position--;
		for(unsigned int i = pos; i < position; i++)
			variables[position] = variables[position + 1];

	}

	void *tag;
	
	unsigned int top; /* top of the stack for the current call frame */
	unsigned int position; /* current position */
	unsigned int length; /* length of the variables array */
	T *variables; // array of variables
};
#endif