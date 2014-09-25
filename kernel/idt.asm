
[BITS 64]
global _idt_load
extern _idtp
_idt_load:
	lidt [_idtp]
	ret
