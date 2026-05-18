#!/bin/bash

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

ROOT_DIR="."
BUILD_DIR="bench/build_release"
BENCH_DIR="bench"
OUTPUT_MD="BENCHMARKS.md"

if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo -e "${RED}Error: This script is only supported on Linux.${NC}"
    exit 1
fi

check_dep() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "${RED}Error: Required tool '$1' not found. Please install it.${NC}"
        exit 1
    fi
}

check_dep "cmake"
check_dep "hyperfine"
check_dep "heaptrack"
check_dep "heaptrack_print"

echo -e "Step 1: Compiling Reduct in Release Mode..."

cmake -S "." -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DREDUCT_USE_SANITIZERS=OFF \
    -DREDUCT_USE_FUZZER=OFF > /dev/null

cmake --build "$BUILD_DIR" --parallel "$(nproc)" > /dev/null

REDUCT_BIN="./$BUILD_DIR/reduct"
if [ ! -f "$REDUCT_BIN" ]; then
    echo -e "${RED}Build failed: Binary not found at $REDUCT_BIN${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful: $REDUCT_BIN created.${NC}"
echo "----------------------------------------------------"

echo -e "Step 2: Running Benchmarks..."
echo -e "## Benchmarks\n" > "$OUTPUT_MD"
echo -e "The included results were automatically generated using the \`run_bench.sh\` script, all benchmarks can be found in \`bench/\`.\n" >> "$OUTPUT_MD" 
echo -e "All benchmarks were performed on the following system:\n" >> "$OUTPUT_MD"
echo -e "- **Timestamp:** \`$(date)\`" >> "$OUTPUT_MD"
echo -e "- **CPU:** \`$(lscpu | grep 'Model name' | cut -f 2 -d ":" | awk '{$1=$1}1')\`" >> "$OUTPUT_MD"
echo -e "- **OS:** \`$(cat /etc/os-release | grep 'PRETTY_NAME' | cut -d '"' -f 2)\`" >> "$OUTPUT_MD"
echo -e "- **Kernel:** \`$(uname -r)\`" >> "$OUTPUT_MD"
echo -e "- **Reduct:** \`$($REDUCT_BIN --version 2>/dev/null || echo "unknown")\`" >> "$OUTPUT_MD"
echo -e "- **Hyperfine:** \`$(hyperfine --version)\`" >> "$OUTPUT_MD"
echo -e "- **Heaptrack:** \`$(heaptrack --version | head -n 1)\`" >> "$OUTPUT_MD"
echo -e "- **Lua:** \`$(lua -v 2>&1 | head -n 1)\`" >> "$OUTPUT_MD"
echo -e "- **LuaJIT:** \`$(luajit -v 2>&1 | head -n 1 || echo "unknown")\`" >> "$OUTPUT_MD"
echo -e "- **Python:** \`$(python3 --version)\`" >> "$OUTPUT_MD"
echo -e "- **Janet:** \`Janet $(janet -v 2>/dev/null || echo "unknown")\`\n" >> "$OUTPUT_MD"

RDT_FILES=$(find "$BENCH_DIR" -maxdepth 1 -name "*.rdt" | sort)

if [ -z "$RDT_FILES" ]; then
    echo -e "${YELLOW}No .rdt files found in $BENCH_DIR. Skipping.${NC}"
    exit 0
fi

for rdt_file in $RDT_FILES; do
    base_name=$(basename "$rdt_file" .rdt)
    echo -e "\n${YELLOW}Benchmarking: $base_name${NC}"

    echo -e "### $base_name\n" >> "$OUTPUT_MD"

    hf_args=()
    bench_cmds=()

    add_bench() {
        hf_args+=(--command-name "$1" "$2")
        bench_cmds+=("$1|$2")
    }

    add_bench "reduct" "$REDUCT_BIN $rdt_file"

    lua_script="$BENCH_DIR/$base_name.lua"
    if [ -f "$lua_script" ]; then
        add_bench "lua" "lua $lua_script"
        if command -v luajit &> /dev/null; then
            add_bench "luajit (jit)" "luajit -jon $lua_script"
            add_bench "luajit (int)" "luajit -joff $lua_script"
        fi
    fi

    python_script="$BENCH_DIR/$base_name.py"
    if [ -f "$python_script" ]; then
        add_bench "python3" "python3 $python_script"
    fi

    janet_script="$BENCH_DIR/$base_name.janet"
    if [ -f "$janet_script" ]; then
        add_bench "janet" "janet $janet_script"
    fi

    echo -e "  - Running performance tests..."
    hyperfine --warmup=10 -N --export-markdown "tmp_hf.md" "${hf_args[@]}"
    cat "tmp_hf.md" >> "$OUTPUT_MD"
    rm "tmp_hf.md"

    echo -e "\n##### Memory Usage\n" >> "$OUTPUT_MD"
    echo -e "| Command | Peak Memory |" >> "$OUTPUT_MD"
    echo -e "|:---|---:|" >> "$OUTPUT_MD"

    for entry in "${bench_cmds[@]}"; do
        name="${entry%%|*}"
        cmd="${entry#*|}"

        heaptrack --record-only $cmd > /dev/null 2>&1
        latest_log=$(ls -t heaptrack.*.zst | head -n 1)
        if [ -f "$latest_log" ]; then
            peak_mem=$(heaptrack_print "$latest_log" | grep "peak heap memory consumption:" | sed 's/peak heap memory consumption: //' | tr -d '[:space:]')
            echo "| \`$name\` | $peak_mem |" >> "$OUTPUT_MD"
            rm "$latest_log"
        else
            echo "| \`$name\` | N/A |" >> "$OUTPUT_MD"
        fi
    done

    echo -e "${GREEN}Done${NC}"
    echo -e "\n---\n" >> "$OUTPUT_MD"
done

echo "----------------------------------------------------"
echo -e "${GREEN}Benchmarks complete! Results saved to ${BOLD}$OUTPUT_MD${NC}"