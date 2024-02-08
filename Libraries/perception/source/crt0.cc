// Some compilers mangle `main` (such as clang), others don't (such as GCC). To be consistent,
// expose a weak `_main` to crt0.asm to calls the mangled main.
int main (int argc, char *argv[]);

extern "C" {

void _main(int argc, char *argv[]) __attribute__((weak)) {
    main(argc, argv);
}

}