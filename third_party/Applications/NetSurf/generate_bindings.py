import os
import sys
import subprocess

def xxd_i(input_path, output_path, var_name):
    print(f"Transpiling {input_path} to {output_path} ({var_name})")
    with open(input_path, "rb") as f:
        data = f.read()
    
    hex_data = []
    for b in data:
        hex_data.append(f"0x{b:02x}")
    
    lines = []
    lines.append(f"unsigned char {var_name}[] = {{")
    for i in range(0, len(hex_data), 12):
        chunk = ", ".join(hex_data[i:i+12])
        lines.append("  " + chunk + ",")
    lines.append("};")
    lines.append(f"unsigned int {var_name}_len = {len(data)};")
    
    with open(output_path, "w") as f:
        f.write("\n".join(lines) + "\n")

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 generate_bindings.py <netsurf_package_root> <nsgenbind_repo_root>")
        sys.exit(1)
        
    netsurf_root = os.path.abspath(sys.argv[1])
    nsgenbind_root = os.path.abspath(sys.argv[2])
    
    print(f"NetSurf Root: {netsurf_root}")
    print(f"nsgenbind Root: {nsgenbind_root}")
    
    # Path to nsgenbind sources
    nsgenbind_src = os.path.join(nsgenbind_root, "src")
    if not os.path.exists(nsgenbind_src):
        # Fallback in case src folder is in root of repo directly
        nsgenbind_src = nsgenbind_root

    print(f"nsgenbind Source Dir: {nsgenbind_src}")
    
    # Compile nsgenbind
    print("1. Compiling nsgenbind host tool...")
    
    # Determine Bison command (prioritize Homebrew Bison 3.x)
    bison_cmd = "/opt/homebrew/opt/bison/bin/bison"
    if not os.path.exists(bison_cmd):
        bison_cmd = "bison"
        
    # Generate Parser & Lexer for nsgenbind
    subprocess.run([
        bison_cmd, "-d", "-t", "--define=api.prefix={nsgenbind_}", 
        "--output=nsgenbind-parser.c", "--defines=nsgenbind-parser.h", 
        "nsgenbind-parser.y"
    ], cwd=nsgenbind_src, check=True)
    
    subprocess.run([
        "flex", "--outfile=nsgenbind-lexer.c", "--header-file=nsgenbind-lexer.h", 
        "nsgenbind-lexer.l"
    ], cwd=nsgenbind_src, check=True)
    
    # Generate Parser & Lexer for WebIDL inside nsgenbind
    subprocess.run([
        bison_cmd, "-d", "-t", "--define=api.prefix={webidl_}", 
        "--output=webidl-parser.c", "--defines=webidl-parser.h", 
        "webidl-parser.y"
    ], cwd=nsgenbind_src, check=True)
    
    subprocess.run([
        "flex", "--outfile=webidl-lexer.c", "--header-file=webidl-lexer.h", 
        "webidl-lexer.l"
    ], cwd=nsgenbind_src, check=True)
    
    # Compile all sources to an executable
    nsgenbind_bin = os.path.join(nsgenbind_src, "nsgenbind")
    c_files = [
        "nsgenbind.c", "utils.c", "output.c", "webidl-ast.c", "nsgenbind-ast.c", "ir.c",
        "duk-libdom.c", "duk-libdom-interface.c", "duk-libdom-dictionary.c",
        "duk-libdom-common.c", "duk-libdom-generated.c", "nsgenbind-parser.c",
        "nsgenbind-lexer.c", "webidl-parser.c", "webidl-lexer.c"
    ]
    
    # Ensure C files list actually matches what's in folder
    compile_cmd = ["clang", "-O2", "-I.", "-o", "nsgenbind"] + c_files
    subprocess.run(compile_cmd, cwd=nsgenbind_src, check=True)
    print("Host nsgenbind successfully compiled!")
    
    # 2. Transpile JS utilities to C headers
    print("2. Transpiling generics.js and polyfill.js...")
    duktape_dir = os.path.join(netsurf_root, "source", "content", "handlers", "javascript", "duktape")
    
    xxd_i(
        os.path.join(duktape_dir, "generics.js"),
        os.path.join(duktape_dir, "generics.js.inc"),
        "generics_js"
    )
    xxd_i(
        os.path.join(duktape_dir, "polyfill.js"),
        os.path.join(duktape_dir, "polyfill.js.inc"),
        "polyfill_js"
    )
    
    # 3. Generate bindings
    print("3. Generating Javascript bindings using nsgenbind...")
    generated_dir = os.path.join(duktape_dir, "generated")
    os.makedirs(generated_dir, exist_ok=True)
    
    idl_dir = os.path.join(netsurf_root, "source", "content", "handlers", "javascript", "WebIDL")
    netsurf_bnd = os.path.join(duktape_dir, "netsurf.bnd")
    
    # Execute nsgenbind
    subprocess.run([
        nsgenbind_bin, "-D", "-I", idl_dir, netsurf_bnd, generated_dir
    ], check=True)
    
    print("Bindings generated successfully!")

if __name__ == "__main__":
    main()
