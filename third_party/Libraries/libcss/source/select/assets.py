# This file is part of LibCSS.
# Licensed under the MIT License,
# http://www.opensource.org/licenses/mit-license.php
# Copyright 2017 Lucas Neves <lcneves@gmail.com>

copyright = '''\
/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2017 The NetSurf Project
 */

'''

def ifndef(name):
	name = name.upper()
	name = f"CSS_COMPUTED_{name}_H_"
	return f"#ifndef {name}\n#define {name}\n"

include_propget = '''\

#include "select/propget.h"
'''

include_calc = '''\

#include "select/calc.h"
'''

calc_unions = '''\

typedef union {
	css_fixed value;
	lwc_string *calc;
} css_fixed_or_calc;
'''

assets = {}

assets['computed.h'] = {}
assets['computed.h']['header'] = copyright + ifndef("computed") + include_calc + calc_unions
assets['computed.h']['footer'] = '\n#endif\n'

assets['propset.h'] = {}
assets['propset.h']['header'] = copyright + ifndef("propset") + include_propget
assets['propset.h']['footer'] = '\n#endif\n'

assets['propget.h'] = {}
assets['propget.h']['header'] = copyright + ifndef("propget")
assets['propget.h']['footer'] = '\n#endif\n'

assets['destroy.inc'] = {}
assets['destroy.inc']['header'] = copyright
assets['destroy.inc']['footer'] = ''
