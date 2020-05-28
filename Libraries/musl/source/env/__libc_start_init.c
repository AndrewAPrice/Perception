extern void call_global_constructors();

static void __libc_start_init() {
	call_global_constructors();
}