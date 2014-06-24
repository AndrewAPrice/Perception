#include "turkey.h"

/* optimizes SSA form, compile with OPTIMIZE_SSA to optimize SSA form after loading a module - there is a preprocessor line in
   ssa_optimizer.cpp for this */

#ifdef OPTIMIZE_SSA
void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
}
#else
void turkey_ssa_optimizer_optimize_function(TurkeyVM *vm, TurkeyFunction *function) {
}
#endif
