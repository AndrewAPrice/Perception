import os
import sys
import subprocess
import shutil

def run_command(args, cwd=None, capture_output=False, input_data=None):
    print(f"Running: {' '.join(args)} (in {cwd or '.'})")
    res = subprocess.run(args, cwd=cwd, capture_output=capture_output, text=True, input=input_data)
    if res.returncode != 0:
        print(f"Command failed with exit code {res.returncode}")
        if capture_output:
            print(f"Stdout:\n{res.stdout}")
            print(f"Stderr:\n{res.stderr}")
        sys.exit(res.returncode)
    return res.stdout if capture_output else None

def find_bison():
    # Check common Homebrew locations on macOS
    paths = [
        "/opt/homebrew/opt/bison/bin/bison",  # Apple Silicon Mac
        "/usr/local/opt/bison/bin/bison",    # Intel Mac
    ]
    for p in paths:
        if os.path.exists(p):
            try:
                out = subprocess.check_output([p, "--version"], text=True)
                if "GNU Bison" in out:
                    print(f"Found Homebrew Bison: {p}")
                    return p
            except Exception:
                pass

    # Fallback to PATH
    bison_path = shutil.which("bison")
    if bison_path:
        try:
            out = subprocess.check_output([bison_path, "--version"], text=True)
            first_line = out.splitlines()[0]
            version_str = first_line.split()[-1]
            version_parts = [int(x) for x in version_str.split('.') if x.isdigit()]
            if version_parts and version_parts[0] >= 3:
                print(f"Found system Bison >= 3.0: {bison_path} (version {version_str})")
                return bison_path
            else:
                print(f"Warning: System bison is too old ({version_str})")
        except Exception as e:
            print(f"Warning checking system bison: {e}")

    print("Error: Bison >= 3.0 is required but not found.")
    print("Please install it via Homebrew: 'brew install bison' and ensure it is available.")
    sys.exit(1)

def find_flex():
    flex_path = shutil.which("flex")
    if flex_path:
        print(f"Found system Flex: {flex_path}")
        return flex_path
    print("Error: Flex is required but not found.")
    sys.exit(1)

def main():
    # 1. Install mako via pip if not already present
    print("Ensuring python mako module is installed...")
    try:
        import mako
        print("  mako is already installed.")
    except ImportError:
        print("  mako not found. Installing...")
        cmd = [sys.executable, "-m", "pip", "install", "mako", "--user", "--index-url", "https://pypi.org/simple/"]
        try:
            run_command(cmd + ["--break-system-packages"])
        except SystemExit:
            run_command(cmd)

    bison = find_bison()
    flex = find_flex()
    python = sys.executable

    package_path = os.getcwd()
    source_path = os.path.join(package_path, "source")

    # 2. Write static helper and wrapper files directly into source/
    print("Writing static helper and wrapper files...")
    git_sha1_h = os.path.join(source_path, "src", "git_sha1.h")
    with open(git_sha1_h, "w") as f:
        f.write('#define MESA_GIT_SHA1 ""\n')

    reallocarray_c = os.path.join(source_path, "src", "util", "reallocarray.c")
    with open(reallocarray_c, "w") as f:
        f.write("""#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

void *reallocarray(void *optr, size_t nmemb, size_t size)
{
    if (nmemb > 0 && size > 0 && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(optr, size * nmemb);
}
""")

    alpha_test_wrapper = os.path.join(source_path, "src", "compiler", "nir", "nir_lower_alpha_test_wrapper.c")
    with open(alpha_test_wrapper, "w") as f:
        f.write('#include "nir_lower_alpha_test.c"\n')

    sp_test_wrapper = os.path.join(source_path, "src", "gallium", "drivers", "softpipe", "sp_quad_depth_test_wrapper.c")
    with open(sp_test_wrapper, "w") as f:
        f.write('#include "sp_quad_depth_test.c"\n')

    # 3. Run Flex/Bison for GLSL
    print("Generating GLSL parser and lexer...")

    # GLSL Parser
    glsl_dir = os.path.join(source_path, "src/compiler/glsl")
    run_command([
        bison,
        "-o", os.path.join(glsl_dir, "glsl_parser.cpp"),
        f"--defines={os.path.join(glsl_dir, 'glsl_parser.h')}",
        "-p", "_mesa_glsl_",
        os.path.join(glsl_dir, "glsl_parser.yy")
    ])

    # GLSL Lexer
    run_command([
        flex,
        "-o", os.path.join(glsl_dir, "glsl_lexer.cpp"),
        os.path.join(glsl_dir, "glsl_lexer.ll")
    ])

    # GLCPP Parser
    glcpp_dir = os.path.join(glsl_dir, "glcpp")
    run_command([
        bison,
        "-o", os.path.join(glcpp_dir, "glcpp-parse.c"),
        f"--defines={os.path.join(glcpp_dir, 'glcpp-parse.h')}",
        "-p", "glcpp_parser_",
        os.path.join(glcpp_dir, "glcpp-parse.y")
    ])

    # GLCPP Lexer
    run_command([
        flex,
        "-o", os.path.join(glcpp_dir, "glcpp-lex.c"),
        os.path.join(glcpp_dir, "glcpp-lex.l")
    ])

    # Program Parser
    program_dir = os.path.join(source_path, "src/mesa/program")
    run_command([
        bison,
        "-o", os.path.join(program_dir, "program_parse.tab.c"),
        f"--defines={os.path.join(program_dir, 'program_parse.tab.h')}",
        "-p", "_mesa_program_",
        os.path.join(program_dir, "program_parse.y")
    ])

    # Program Lexer
    run_command([
        flex,
        "-o", os.path.join(program_dir, "program_lexer.c"),
        os.path.join(program_dir, "program_lexer.l")
    ])

    # 4. Run Python generators for IR expression operations
    print("Generating IR expression operation headers...")
    compiler_dir = os.path.join(source_path, "src/compiler")

    # ir_expression_operation.h
    out = run_command([python, os.path.join(glsl_dir, "ir_expression_operation.py"), "enum"], capture_output=True)
    with open(os.path.join(compiler_dir, "ir_expression_operation.h"), "w") as f:
        f.write(out)

    # ir_expression_operation_constant.h
    out = run_command([python, os.path.join(glsl_dir, "ir_expression_operation.py"), "constant"], capture_output=True)
    with open(os.path.join(glsl_dir, "ir_expression_operation_constant.h"), "w") as f:
        f.write(out)

    # ir_expression_operation_strings.h
    out = run_command([python, os.path.join(glsl_dir, "ir_expression_operation.py"), "strings"], capture_output=True)
    with open(os.path.join(glsl_dir, "ir_expression_operation_strings.h"), "w") as f:
        f.write(out)

    # 5. Run Python generators for NIR
    print("Generating NIR headers and sources...")
    nir_dir = os.path.join(source_path, "src/compiler/nir")

    # nir_builder_opcodes.h
    out = run_command([python, os.path.join(nir_dir, "nir_builder_opcodes_h.py")], capture_output=True)
    with open(os.path.join(nir_dir, "nir_builder_opcodes.h"), "w") as f:
        f.write(out)

    # nir_constant_expressions.c
    out = run_command([python, os.path.join(nir_dir, "nir_constant_expressions.py")], capture_output=True)
    with open(os.path.join(nir_dir, "nir_constant_expressions.c"), "w") as f:
        f.write(out)

    # nir_opcodes.h
    out = run_command([python, os.path.join(nir_dir, "nir_opcodes_h.py")], capture_output=True)
    with open(os.path.join(nir_dir, "nir_opcodes.h"), "w") as f:
        f.write(out)

    # nir_opcodes.c
    out = run_command([python, os.path.join(nir_dir, "nir_opcodes_c.py")], capture_output=True)
    with open(os.path.join(nir_dir, "nir_opcodes.c"), "w") as f:
        f.write(out)

    # nir_opt_algebraic.c
    out = run_command([python, os.path.join(nir_dir, "nir_opt_algebraic.py")], capture_output=True)
    with open(os.path.join(nir_dir, "nir_opt_algebraic.c"), "w") as f:
        f.write(out)

    # nir_intrinsics.h
    run_command([python, os.path.join(nir_dir, "nir_intrinsics_h.py"), "--out", nir_dir])

    # nir_intrinsics.c
    run_command([python, os.path.join(nir_dir, "nir_intrinsics_c.py"), "--out", nir_dir])

    # nir_intrinsics_indices.h
    run_command([python, os.path.join(nir_dir, "nir_intrinsics_indices_h.py"), "--out", nir_dir])

    # 6. Run Python generators for GLAPI
    print("Generating GLAPI headers and sources...")
    gen_dir = os.path.join(source_path, "src/mapi/glapi/gen")
    main_dir = os.path.join(source_path, "src/mesa/main")

    def run_gen(script, args, output_file):
        out = run_command([python, script] + args, cwd=gen_dir, capture_output=True)
        out_abs = os.path.join(gen_dir, output_file)
        os.makedirs(os.path.dirname(out_abs), exist_ok=True)
        with open(out_abs, "w") as f:
            f.write(out)

    # glapi_mapi_tmp.h
    run_gen("../../mapi_abi.py", ["--printer", "glapi", "gl_and_es_API.xml"], "../../glapi_mapi_tmp.h")

    # glprocs.h
    run_gen("gl_procs.py", ["-c", "-f", "gl_and_es_API.xml"], "../glprocs.h")

    # glapitemp.h
    run_gen("gl_apitemp.py", ["-f", "gl_and_es_API.xml"], "../glapitemp.h")

    # glapitable.h
    run_gen("gl_table.py", ["-f", "gl_and_es_API.xml"], "../glapitable.h")

    # glapi_gentable.c
    run_gen("gl_gentable.py", ["-f", "gl_and_es_API.xml"], "../glapi_gentable.c")

    # enums.c
    run_gen("gl_enums.py", ["-f", "../registry/gl.xml"], "../../../mesa/main/enums.c")

    # api_exec_init.c
    run_gen("api_exec_init.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/api_exec_init.c")

    # api_exec_decl.h
    run_gen("api_exec_decl_h.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/api_exec_decl.h")

    # api_save_init.h
    run_gen("api_save_init_h.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/api_save_init.h")

    # api_save.h
    run_gen("api_save_h.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/api_save.h")

    # api_beginend_init.h
    run_gen("api_beginend_init_h.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/api_beginend_init.h")

    # api_hw_select_init.h
    run_gen("api_hw_select_init_h.py", ["-f", "gl_API.xml"], "../../../mesa/main/api_hw_select_init.h")

    # unmarshal_table.c
    run_gen("gl_unmarshal_table.py", ["gl_and_es_API.xml"], "../../../mesa/main/unmarshal_table.c")

    # marshal_generated{0-7}.c
    for x in range(8):
        run_gen("gl_marshal.py", ["gl_and_es_API.xml", str(x), "8"], f"../../../mesa/main/marshal_generated{x}.c")

    # 7. Generate SPIR-V files
    print("Generating SPIR-V files...")
    spirv_dir = os.path.join(source_path, "src/compiler/spirv")
    run_command([python, os.path.join(spirv_dir, "vtn_generator_ids_h.py"),
                 os.path.join(spirv_dir, "spir-v.xml"),
                 os.path.join(spirv_dir, "vtn_generator_ids.h")])
    run_command([python, os.path.join(spirv_dir, "spirv_info_c.py"),
                 os.path.join(spirv_dir, "spirv.core.grammar.json"),
                 os.path.join(spirv_dir, "spirv_info.c")])
    run_command([python, os.path.join(spirv_dir, "vtn_gather_types_c.py"),
                 os.path.join(spirv_dir, "spirv.core.grammar.json"),
                 os.path.join(spirv_dir, "vtn_gather_types.c")])

    # 8. Generate Mesa Core files
    print("Generating Mesa Core files...")
    out = run_command([python, os.path.join(main_dir, "format_info.py"),
                       os.path.join(main_dir, "formats.csv")], capture_output=True)
    with open(os.path.join(main_dir, "format_info.h"), "w") as f:
        f.write(out)

    out = run_command([python, os.path.join(main_dir, "get_hash_generator.py"), "-f",
                       os.path.join(package_path, "source/src/mapi/glapi/gen/gl_and_es_API.xml")], capture_output=True)
    with open(os.path.join(main_dir, "get_hash.h"), "w") as f:
        f.write(out)

    run_command([python, os.path.join(main_dir, "format_fallback.py"),
                 os.path.join(main_dir, "formats.csv"),
                 os.path.join(main_dir, "format_fallback.c")])

    run_gen("gl_table.py", ["-f", "gl_and_es_API.xml", "-m", "remap_table"], "../../../mesa/main/dispatch.h")
    run_gen("gl_marshal_h.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/marshal_generated.h")
    run_gen("remap_helper.py", ["-f", "gl_and_es_API.xml"], "../../../mesa/main/remap_helper.h")

    # 9. Generate Util files
    print("Generating Util files...")
    util_dir = os.path.join(source_path, "src/util")
    run_command([python, os.path.join(util_dir, "driconf_static.py"),
                 os.path.join(util_dir, "00-mesa-defaults.conf"),
                 os.path.join(util_dir, "driconf_static.h")])
    out = run_command([python, os.path.join(util_dir, "format_srgb.py")], capture_output=True)
    with open(os.path.join(util_dir, "format_srgb.c"), "w") as f:
        f.write(out)

    # 10. Generate Gallium Auxiliary files
    print("Generating Gallium Auxiliary files...")
    aux_dir = os.path.join(source_path, "src/gallium/auxiliary")
    tracepoints_dir = os.path.join(aux_dir, "tracepoints")
    os.makedirs(tracepoints_dir, exist_ok=True)
    run_command([python, os.path.join(aux_dir, "util/u_tracepoints.py"),
                 "-p", os.path.join(util_dir, "perf"),
                 "-H", os.path.join(tracepoints_dir, "u_tracepoints.h")])
    run_command([python, os.path.join(aux_dir, "util/u_tracepoints.py"),
                 "-p", os.path.join(util_dir, "perf"),
                 "-C", os.path.join(tracepoints_dir, "u_tracepoints.c")])

    run_command([python, os.path.join(aux_dir, "indices/u_indices_gen.py"),
                 os.path.join(aux_dir, "indices/u_indices_gen.c")])
    run_command([python, os.path.join(aux_dir, "indices/u_unfilled_gen.py"),
                 os.path.join(aux_dir, "indices/u_unfilled_gen.c")])

    driver_trace_dir = os.path.join(source_path, "src/gallium/auxiliary/driver_trace")
    run_command([python, os.path.join(driver_trace_dir, "enums2names.py"),
                 os.path.join(source_path, "src/gallium/include/pipe/p_defines.h"),
                 "-C", os.path.join(driver_trace_dir, "tr_util.c"),
                 "-H", os.path.join(driver_trace_dir, "tr_util.h"),
                 "-I", "tr_util.h"])

    # 11. Generate float64_glsl.h
    print("Generating float64_glsl.h...")
    run_command([python, os.path.join(util_dir, "xxd.py"),
                 os.path.join(glsl_dir, "float64.glsl"),
                 os.path.join(glsl_dir, "float64_glsl.h"),
                 "-n", "float64_source"])

    # 12. Generate Gallium Format files
    print("Generating Gallium Format files...")
    format_dir = os.path.join(source_path, "src/util/format")
    out = run_command([python, os.path.join(format_dir, "u_format_table.py"),
                       os.path.join(format_dir, "u_format.csv"), "--header"], capture_output=True)
    with open(os.path.join(format_dir, "u_format_pack.h"), "w") as f:
        f.write(out)
    out = run_command([python, os.path.join(format_dir, "u_format_table.py"),
                       os.path.join(format_dir, "u_format.csv")], capture_output=True)
    with open(os.path.join(format_dir, "u_format_table.c"), "w") as f:
        f.write(out)

    print("Mesa code generation complete!")

if __name__ == "__main__":
    main()
