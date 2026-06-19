# Sparse-Conditional-Constant-Propagation-and-CFG-Simplification
Project for the Compiler Construction course at the Faculty of Mathematics on the topic of Sparse Conditional Constant Propagation (SCCP) and CFG simplification.

**Sparse Conditional Constant Propagation (SCCP)** is a compiler optimization technique that combines constant propagation with reachability analysis. By analyzing only feasible execution paths, SCCP identifies variables that remain constant at specific program points and replaces them with their actual values.  This often enables further optimizations such as dead code elimination and branch simplification.

**Control-Flow Graph (CFG) Simplification** is an optimization that reduces the structural complexity of a program's control-flow graph. It removes unreachable basic blocks, merges redundant branches and linear sequences of blocks, and eliminates unnecessary control-flow constructs. These transformations improve code readability and create opportunities for additional compiler optimizations.

The two optimizations complement each other: SCCP can expose unreachable code and simplify conditional branches, while CFG Simplification removes the resulting redundant control-flow structures, enabling more effective optimization of the generated IR.


## Build and Run Instructions

First, download the LLVM project by running the script provided by the course:

```bash
wget https://www.prevodioci.matf.bg.ac.rs/kk/2023/vezbe/download_llvm.sh
chmod +x download_llvm.sh
./download_llvm.sh
```

After the LLVM project is downloaded, build it using the provided build script:

```bash
chmod +x make_llvm.sh
./make_llvm.sh
```

The script creates a `build` directory and builds LLVM together with Clang.

After that, copy the files from this project into the LLVM source tree. Inside the directory:

```text
llvm/lib/Transforms/
```

create a new folder named:

```text
OurPass
```

Then copy the implementation files from the `src` directory into this folder:

```text
src/CMakeLists.txt
src/OurSCCPPass.cpp
src/OurSCCPPass.h
src/OurSimplifyCFGPass.cpp
```

The final structure should look like this:

```text
llvm/lib/Transforms/OurPass/
├── CMakeLists.txt
├── OurSCCPPass.cpp
├── OurSCCPPass.h
└── OurSimplifyCFGPass.cpp
```

Next, open the file:

```text
llvm/lib/Transforms/CMakeLists.txt
```

and add the following line:

```cmake
add_subdirectory(OurPass)
```

This registers the custom optimization pass inside the LLVM build system.

After adding the pass, rebuild LLVM by running:

```bash
./make_llvm.sh
```

When the build is finished, check whether the pass library was created:

```bash
cd build
find . -name "LLVMOurPass.so"
```

The expected result is:

```text
./lib/LLVMOurPass.so
```

The project also contains a testing script named:

```text
run_tests.sh
```

and a folder named:

```text
tests
```

Copy both of them into the `build` directory.

Then, from the `build` directory, give execution permission to the testing script:

```bash
chmod +x run_tests.sh
```

Run the tests with:

```bash
./run_tests.sh
```

The script automatically performs the following steps for each `.c` file from the `tests` folder:

1. Converts the C source file to LLVM IR (`.ll`)
2. Runs the `mem2reg` pass to convert the IR into SSA form
3. Runs the custom SCCP and CFG simplification passes
4. Verifies the final LLVM IR

After the script finishes, a new folder will be created:

```text
test_outputs
```

Inside this folder, the generated files can be found:

```text
example.ll
example_ssa.ll
example_full.ll
```

The file with the suffix `_full.ll` contains the final optimized LLVM IR after running both custom passes.
