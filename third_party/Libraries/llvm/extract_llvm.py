import os
import sys
import subprocess
import shutil
import glob

HOST_CONFIG_H = """#ifndef CONFIG_H
#define CONFIG_H
#define BUG_REPORT_URL "https://github.com/llvm/llvm-project/issues/"
#define ENABLE_BACKTRACES 1
#define HAVE_BACKTRACE 1
#define BACKTRACE_HEADER <execinfo.h>
#define HAVE_DLFCN_H 1
#define HAVE_DLYLD_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FENV_H 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GETRLIMIT 1
#define HAVE_GETRUSAGE 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LINK_H 0
#define HAVE_LSTAT 1
#define HAVE_MALLOC_ZONE_STATISTICS 1
#define HAVE_POLL_H 1
#define HAVE_POSIX_SPAWN 1
#define HAVE_PTHREAD_H 1
#define HAVE_PTHREAD_MUTEX_LOCK 1
#define HAVE_PTHREAD_RWLOCK_INIT 1
#define HAVE_SIGALTSTACK 1
#define HAVE_SIGNAL_H 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYSEXITS_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VALGRIND_VALGRIND_H 0
#define HAVE__UNWIND_BACKTRACE 1
#define PACKAGE_BUGREPORT "https://github.com/llvm/llvm-project/issues/"
#define PACKAGE_NAME "LLVM"
#define PACKAGE_STRING "LLVM 16.0.0git"
#define PACKAGE_TARNAME "llvm"
#define PACKAGE_VERSION "16.0.0git"
#define REASONABLE_STACK_LIMIT 2048 * 1024
#endif
"""

HOST_LLVM_CONFIG_H = """#ifndef LLVM_CONFIG_H
#define LLVM_CONFIG_H
#define LLVM_DEFAULT_TARGET_TRIPLE "x86_64-apple-darwin"
#define LLVM_HOST_TRIPLE "x86_64-apple-darwin"
#define LLVM_ON_UNIX 1
#define LLVM_VERSION_MAJOR 20
#define LLVM_VERSION_MINOR 1
#define LLVM_VERSION_PATCH 0
#define LLVM_VERSION_STRING "20.1.0"
#define LLVM_HAS_ATOMICS 1
#define LLVM_ENABLE_THREADS 1
#define LLVM_ENABLE_CRASH_DUMPS 0
#define LLVM_WINDOWS_PREFER_FORWARD_SLASH 0
#define HAVE_SYSEXITS_H 1
#endif
"""

TARGET_CONFIG_H = """#ifndef CONFIG_H
#define CONFIG_H
#define BUG_REPORT_URL "https://github.com/llvm/llvm-project/issues/"
#define ENABLE_BACKTRACES 0
/* #undef HAVE_BACKTRACE */
#define HAVE_DLFCN_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FENV_H 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GETRLIMIT 1
#define HAVE_GETRUSAGE 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LINK_H 1
#define HAVE_LSTAT 1
#define HAVE_POLL_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_PTHREAD_MUTEX_LOCK 1
#define HAVE_PTHREAD_RWLOCK_INIT 1
#define HAVE_SIGALTSTACK 1
#define HAVE_SIGNAL_H 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYSEXITS_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VALGRIND_VALGRIND_H 0
#define PACKAGE_BUGREPORT "https://github.com/llvm/llvm-project/issues/"
#define PACKAGE_NAME "LLVM"
#define PACKAGE_STRING "LLVM 20.1.0"
#define PACKAGE_TARNAME "llvm"
#define PACKAGE_VERSION "20.1.0"
#define REASONABLE_STACK_LIMIT 2048 * 1024
#endif
"""

TARGET_LLVM_CONFIG_H = """#ifndef LLVM_CONFIG_H
#define LLVM_CONFIG_H
#define LLVM_DEFAULT_TARGET_TRIPLE "x86_64-unknown-perception"
#define LLVM_HOST_TRIPLE "x86_64-unknown-perception"
#define LLVM_ON_UNIX 1
#define LLVM_VERSION_MAJOR 20
#define LLVM_VERSION_MINOR 1
#define LLVM_VERSION_PATCH 0
#define LLVM_VERSION_STRING "20.1.0"
#define LLVM_HAS_ATOMICS 1
#define LLVM_ENABLE_THREADS 1
#define LLVM_ENABLE_CRASH_DUMPS 0
#define LLVM_WINDOWS_PREFER_FORWARD_SLASH 0
#define HAVE_SYSEXITS_H 1
#endif
"""

ABI_BREAKING_H = """#ifndef LLVM_ABI_BREAKING_H
#define LLVM_ABI_BREAKING_H
#define LLVM_ENABLE_ABI_BREAKING_CHECKS 0
#define LLVM_ENABLE_REVERSE_ITERATION 0
#endif
"""

def compile_sources(sources, include_flags, build_temp, output_bin):
    """Compiles C and C++ sources separately and links them."""
    objs = []
    print(f"Compiling {len(sources)} source files...")
    for src in sources:
        safe_name = src.replace(os.sep, "_").replace(":", "_") + ".o"
        obj = os.path.join(build_temp, safe_name)
        objs.append(obj)

        if src.endswith('.c'):
            cmd = ["/usr/bin/clang", "-O2"] + include_flags + ["-c", src, "-o", obj]
        else:
            cmd = [
                "/usr/bin/clang++", "-O2", "-std=c++17",
                "-D__STDC_CONSTANT_MACROS", "-D__STDC_LIMIT_MACROS"
            ] + include_flags + ["-c", src, "-o", obj]

        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"Failed to compile {src}:")
            print(res.stderr)
            sys.exit(1)

    print(f"Linking {output_bin}...")
    link_cmd = ["/usr/bin/clang++"] + objs + ["-o", output_bin]
    res = subprocess.run(link_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Failed to link {output_bin}:")
        print(res.stderr)
        sys.exit(1)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract_llvm.py <llvm_source_path>")
        sys.exit(1)

    llvm_source_path = os.path.abspath(sys.argv[1])
    if not os.path.exists(llvm_source_path):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        workspace_root = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))
        alt_path = os.path.abspath(os.path.join(workspace_root, sys.argv[1]))
        if os.path.exists(alt_path):
            llvm_source_path = alt_path

    package_path = os.getcwd()

    print(f"LLVM Source Path: {llvm_source_path}")
    print(f"Package Path: {package_path}")

    # 1. Create target configuration headers directly in generated/
    print("Generating target configuration headers...")
    target_config_dir = os.path.join(package_path, "generated", "llvm", "Config")
    os.makedirs(target_config_dir, exist_ok=True)
    with open(os.path.join(target_config_dir, "config.h"), "w") as f:
        f.write(TARGET_CONFIG_H)
    with open(os.path.join(target_config_dir, "llvm-config.h"), "w") as f:
        f.write(TARGET_LLVM_CONFIG_H)
    with open(os.path.join(target_config_dir, "abi-breaking.h"), "w") as f:
        f.write(ABI_BREAKING_H)

    # Generate machine/endian.h redirect to musl's <endian.h>
    target_machine_dir = os.path.join(package_path, "generated", "machine")
    os.makedirs(target_machine_dir, exist_ok=True)
    with open(os.path.join(target_machine_dir, "endian.h"), "w") as f:
        f.write("#include <endian.h>\n")

    # Generate target .def files
    with open(os.path.join(target_config_dir, "Targets.def"), "w") as f:
        f.write("#ifndef LLVM_TARGET\n#define LLVM_TARGET(TargetName)\n#endif\nLLVM_TARGET(X86)\n#undef LLVM_TARGET\n")
    with open(os.path.join(target_config_dir, "AsmPrinters.def"), "w") as f:
        f.write("#ifndef LLVM_ASM_PRINTER\n#define LLVM_ASM_PRINTER(TargetName)\n#endif\nLLVM_ASM_PRINTER(X86)\n#undef LLVM_ASM_PRINTER\n")
    with open(os.path.join(target_config_dir, "AsmParsers.def"), "w") as f:
        f.write("#ifndef LLVM_ASM_PARSER\n#define LLVM_ASM_PARSER(TargetName)\n#endif\nLLVM_ASM_PARSER(X86)\n#undef LLVM_ASM_PARSER\n")
    with open(os.path.join(target_config_dir, "Disassemblers.def"), "w") as f:
        f.write("#ifndef LLVM_DISASSEMBLER\n#define LLVM_DISASSEMBLER(TargetName)\n#endif\nLLVM_DISASSEMBLER(X86)\n#undef LLVM_DISASSEMBLER\n")
    with open(os.path.join(target_config_dir, "TargetMCAs.def"), "w") as f:
        f.write("#ifndef LLVM_TARGETMCA\n#define LLVM_TARGETMCA(TargetName)\n#endif\n#undef LLVM_TARGETMCA\n")

    # Make sure VCSRevision.h and Extension.def exist
    target_support_dir = os.path.join(package_path, "generated", "llvm", "Support")
    os.makedirs(target_support_dir, exist_ok=True)
    with open(os.path.join(target_support_dir, "VCSRevision.h"), "w") as f:
        f.write("#undef LLVM_REVISION\n")
    with open(os.path.join(target_support_dir, "Extension.def"), "w") as f:
        f.write("#ifndef HANDLE_EXTENSION\n#define HANDLE_EXTENSION(Class)\n#endif\n#undef HANDLE_EXTENSION\n")

    # 2. Compile host TableGen tools (Two-stage bootstrap)
    build_temp = os.path.join(package_path, "build_tblgen")
    if os.path.exists(build_temp):
        shutil.rmtree(build_temp)
    os.makedirs(build_temp)

    # Write host config headers
    host_config_dir = os.path.join(build_temp, "llvm", "Config")
    os.makedirs(host_config_dir, exist_ok=True)
    with open(os.path.join(host_config_dir, "config.h"), "w") as f:
        f.write(HOST_CONFIG_H)
    with open(os.path.join(host_config_dir, "llvm-config.h"), "w") as f:
        f.write(HOST_LLVM_CONFIG_H)
    with open(os.path.join(host_config_dir, "abi-breaking.h"), "w") as f:
        f.write(ABI_BREAKING_H)

    host_support_dir = os.path.join(build_temp, "llvm", "Support")
    os.makedirs(host_support_dir, exist_ok=True)
    with open(os.path.join(host_support_dir, "VCSRevision.h"), "w") as f:
        f.write("#undef LLVM_REVISION\n")

    include_flags = [
        f"-I{build_temp}",
        f"-I{os.path.join(llvm_source_path, 'include')}",
        f"-I{os.path.join(llvm_source_path, 'lib', 'Support')}",
        f"-I{os.path.abspath(os.path.join(llvm_source_path, '..', 'libc'))}",
        f"-I{os.path.abspath(os.path.join(llvm_source_path, '..', 'third-party', 'siphash', 'include'))}",
    ]

    # STAGE 1: Compile llvm-min-tblgen
    print("Building host llvm-min-tblgen (Stage 1)...")
    min_tblgen_sources = []
    min_tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "Support", "*.cpp")))
    min_tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "Support", "*.c")))
    min_tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "TableGen", "*.cpp")))
    min_tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "utils", "TableGen", "Basic", "*.cpp")))
    min_tblgen_sources.append(os.path.join(llvm_source_path, "utils", "TableGen", "llvm-min-tblgen.cpp"))

    min_tblgen_bin = os.path.join(build_temp, "llvm-min-tblgen")
    compile_sources(min_tblgen_sources, include_flags, build_temp, min_tblgen_bin)
    print("Host llvm-min-tblgen compiled successfully!")

    # STAGE 2: Run llvm-min-tblgen to generate GenVT.inc
    print("Generating GenVT.inc (Stage 2)...")
    codegen_inc_dir = os.path.join(build_temp, "llvm", "CodeGen")
    os.makedirs(codegen_inc_dir, exist_ok=True)

    gen_vt_cmd = [
        min_tblgen_bin,
        "-gen-vt",
        f"-I{os.path.join(llvm_source_path, 'include')}",
        os.path.join(llvm_source_path, "include", "llvm", "CodeGen", "ValueTypes.td"),
        "-o", os.path.join(codegen_inc_dir, "GenVT.inc")
    ]
    result = subprocess.run(gen_vt_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("Failed to generate GenVT.inc:")
        print(result.stderr)
        sys.exit(1)
    print("GenVT.inc generated successfully!")

    target_codegen_dir = os.path.join(package_path, "generated", "llvm", "CodeGen")
    os.makedirs(target_codegen_dir, exist_ok=True)
    shutil.copy2(os.path.join(codegen_inc_dir, "GenVT.inc"), os.path.join(target_codegen_dir, "GenVT.inc"))

    # STAGE 3: Compile full llvm-tblgen
    print("Building host llvm-tblgen (Stage 3)...")
    tblgen_sources = []
    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "Support", "*.cpp")))
    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "Support", "*.c")))
    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "TableGen", "*.cpp")))
    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "lib", "CodeGenTypes", "*.cpp")))

    for f in glob.glob(os.path.join(llvm_source_path, "utils", "TableGen", "*.cpp")):
        if not f.endswith("llvm-min-tblgen.cpp"):
            tblgen_sources.append(f)

    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "utils", "TableGen", "Common", "**", "*.cpp"), recursive=True))
    tblgen_sources.extend(glob.glob(os.path.join(llvm_source_path, "utils", "TableGen", "Basic", "*.cpp")))

    full_include_flags = include_flags + [
        f"-I{os.path.join(llvm_source_path, 'lib', 'TableGen')}",
        f"-I{os.path.join(llvm_source_path, 'utils', 'TableGen')}",
        f"-I{os.path.join(llvm_source_path, 'utils', 'TableGen', 'Common')}",
    ]

    tblgen_bin = os.path.join(build_temp, "llvm-tblgen")
    compile_sources(tblgen_sources, full_include_flags, build_temp, tblgen_bin)
    print("Host llvm-tblgen compiled successfully!")

    # 3. Run TableGen to generate .inc files
    print("Running TableGen to generate .inc files...")
    ir_td_path = os.path.join(llvm_source_path, "include", "llvm", "IR")
    ir_dest_dir = os.path.join(package_path, "generated", "llvm", "IR")
    os.makedirs(ir_dest_dir, exist_ok=True)

    def run_tblgen(td_file, action, out_file, extra_args=[]):
        cmd = [
            tblgen_bin,
            f"-I{os.path.join(llvm_source_path, 'include')}",
            f"-I{ir_td_path}",
            action,
            td_file,
            "-o", out_file
        ] + extra_args
        run_res = subprocess.run(cmd, capture_output=True, text=True)
        if run_res.returncode != 0:
            print(f"TableGen failed on {td_file} with action {action}:")
            print(run_res.stderr)
            sys.exit(1)

    print("  Generating IR TableGen files...")
    run_tblgen(os.path.join(ir_td_path, "Attributes.td"), "-gen-attrs", os.path.join(ir_dest_dir, "Attributes.inc"))
    run_tblgen(os.path.join(ir_td_path, "Intrinsics.td"), "-gen-intrinsic-impl", os.path.join(ir_dest_dir, "IntrinsicImpl.inc"))
    run_tblgen(os.path.join(ir_td_path, "Intrinsics.td"), "-gen-intrinsic-enums", os.path.join(ir_dest_dir, "IntrinsicEnums.inc"))

    print("  Generating target-specific intrinsic headers...")
    TARGET_INTRINSICS = [
        ("aarch64", "IntrinsicsAArch64.h"),
        ("amdgcn", "IntrinsicsAMDGPU.h"),
        ("arm", "IntrinsicsARM.h"),
        ("bpf", "IntrinsicsBPF.h"),
        ("dx", "IntrinsicsDirectX.h"),
        ("hexagon", "IntrinsicsHexagon.h"),
        ("loongarch", "IntrinsicsLoongArch.h"),
        ("mips", "IntrinsicsMips.h"),
        ("nvvm", "IntrinsicsNVPTX.h"),
        ("ppc", "IntrinsicsPowerPC.h"),
        ("r600", "IntrinsicsR600.h"),
        ("riscv", "IntrinsicsRISCV.h"),
        ("spv", "IntrinsicsSPIRV.h"),
        ("s390", "IntrinsicsS390.h"),
        ("wasm", "IntrinsicsWebAssembly.h"),
        ("x86", "IntrinsicsX86.h"),
        ("xcore", "IntrinsicsXCore.h"),
        ("ve", "IntrinsicsVE.h"),
    ]
    for prefix, filename in TARGET_INTRINSICS:
        run_tblgen(os.path.join(ir_td_path, "Intrinsics.td"), "-gen-intrinsic-enums",
                   os.path.join(ir_dest_dir, filename), [f"-intrinsic-prefix={prefix}"])

    x86_td_path = os.path.join(llvm_source_path, "lib", "Target", "X86")
    x86_dest_dir = os.path.join(package_path, "generated", "llvm", "Target", "X86")
    os.makedirs(x86_dest_dir, exist_ok=True)

    print("  Generating X86 Target TableGen files...")
    x86_target_td = os.path.join(x86_td_path, "X86.td")
    x86_args = [f"-I{x86_td_path}", f"-I{os.path.join(llvm_source_path, 'lib', 'Target')}"]

    run_tblgen(x86_target_td, "-gen-asm-matcher", os.path.join(x86_dest_dir, "X86GenAsmMatcher.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-asm-writer", os.path.join(x86_dest_dir, "X86GenAsmWriter.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-asm-writer", os.path.join(x86_dest_dir, "X86GenAsmWriter1.inc"), x86_args + ["-asmwriternum=1"])
    run_tblgen(x86_target_td, "-gen-callingconv", os.path.join(x86_dest_dir, "X86GenCallingConv.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-dag-isel", os.path.join(x86_dest_dir, "X86GenDAGISel.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-disassembler", os.path.join(x86_dest_dir, "X86GenDisassemblerTables.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-fast-isel", os.path.join(x86_dest_dir, "X86GenFastISel.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-instr-info", os.path.join(x86_dest_dir, "X86GenInstrInfo.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-register-bank", os.path.join(x86_dest_dir, "X86GenRegisterBank.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-register-info", os.path.join(x86_dest_dir, "X86GenRegisterInfo.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-subtarget", os.path.join(x86_dest_dir, "X86GenSubtargetInfo.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-x86-instr-mapping", os.path.join(x86_dest_dir, "X86GenInstrMapping.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-global-isel", os.path.join(x86_dest_dir, "X86GenGlobalISel.inc"), x86_args)
    run_tblgen(x86_target_td, "-gen-x86-fold-tables", os.path.join(x86_dest_dir, "X86GenFoldTables.inc"), x86_args + ["-asmwriternum=1"])
    run_tblgen(x86_target_td, "-gen-x86-mnemonic-tables", os.path.join(x86_dest_dir, "X86GenMnemonicTables.inc"), x86_args + ["-asmwriternum=1"])

    omp_td = os.path.join(llvm_source_path, "include", "llvm", "Frontend", "OpenMP", "OMP.td")
    omp_dest_dir = os.path.join(package_path, "public", "llvm", "Frontend", "OpenMP")
    os.makedirs(omp_dest_dir, exist_ok=True)
    run_tblgen(omp_td, "-gen-directive-decl", os.path.join(omp_dest_dir, "OMP.h.inc"))
    run_tblgen(omp_td, "-gen-directive-impl", os.path.join(omp_dest_dir, "OMP.inc"))

    # Cleanup temporary build dir
    if os.path.exists(build_temp):
        shutil.rmtree(build_temp, ignore_errors=True)

    print("LLVM TableGen generation complete!")

if __name__ == "__main__":
    main()
