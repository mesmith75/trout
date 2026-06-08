build_dir := "build"
phlex_lib := if env("PHLEX_PLUGIN_PATH", "") != "" { env("PHLEX_PLUGIN_PATH", "") } else { `phlex --version 2>&1 > /dev/null; dirname $(which phlex)`+ "/../lib" }
plugin_path := build_dir + ":" + phlex_lib
events := "10000"
g4_events := "100"
g4_concurrency := "4"

_phlex *args:
    PHLEX_PLUGIN_PATH={{plugin_path}} phlex {{args}}

_phlex_jsonnet workflow *ext_args:
    PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet {{ext_args}} {{workflow}})

# Build all targets
build:
    cmake --build {{build_dir}}

# Build only the standalone benchmark
build-benchmark:
    cmake --build {{build_dir}} --target pythia8_benchmark

# --- Pythia8 benchmarks ---

# Compare standalone Pythia8 vs Phlex workflows
bench-pythia8: build
    hyperfine \
        --warmup 1 \
        --min-runs 5 \
        --reference './{{build_dir}}/pythia8_benchmark --events {{events}}' \
        --reference-name 'standalone' \
        -n 'phlex noop' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{events}} workflows/pythia8_noop.jsonnet)' \
        -n 'phlex mc_only' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{events}} workflows/pythia8_only.jsonnet)' \
        --export-markdown bench-pythia8.md \
        --export-json bench-pythia8.json

# Benchmark standalone Pythia8 serial vs parallel
bench-standalone: build-benchmark
    hyperfine \
        --warmup 1 \
        --min-runs 5 \
        -n 'serial' './{{build_dir}}/pythia8_benchmark --events {{events}}' \
        -n 'parallel 4T' './{{build_dir}}/pythia8_benchmark --events {{events}} --threads 4' \
        -n 'parallel 8T' './{{build_dir}}/pythia8_benchmark --events {{events}} --threads 8' \
        --export-markdown bench-standalone.md

# Benchmark Phlex I/O overhead: noop vs mc_only
bench-phlex-io: build
    hyperfine \
        --warmup 1 \
        --min-runs 5 \
        --reference 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{events}} workflows/pythia8_noop.jsonnet)' \
        --reference-name 'phlex noop' \
        -n 'phlex mc_only' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{events}} workflows/pythia8_only.jsonnet)' \
        --export-markdown bench-phlex-io.md

# Benchmark FairShip-like vs default Pythia8 config
bench-fairship: build-benchmark
    hyperfine \
        --warmup 1 \
        --min-runs 5 \
        -n 'default' './{{build_dir}}/pythia8_benchmark --events {{events}}' \
        -n 'fairship' './{{build_dir}}/pythia8_benchmark --events {{events}} --fairship' \
        --export-markdown bench-fairship.md

# Sweep event counts to measure startup vs per-event cost
bench-scaling: build
    hyperfine \
        --warmup 1 \
        --min-runs 3 \
        -L events 100,1000,10000,100000 \
        -n 'standalone {events}' './{{build_dir}}/pythia8_benchmark --events {events}' \
        --export-markdown bench-scaling-standalone.md
    hyperfine \
        --warmup 1 \
        --min-runs 3 \
        -L events 100,1000,10000,100000 \
        -n 'phlex noop {events}' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={events} workflows/pythia8_noop.jsonnet)' \
        --export-markdown bench-scaling-phlex.md

# --- Geant4 benchmarks ---

# Compare G4 single-threaded vs multi-threaded (noop output, no I/O)
bench-geant4: build
    hyperfine \
        --warmup 1 \
        --min-runs 3 \
        --reference 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} workflows/gun_st_bench.jsonnet)' \
        --reference-name 'G4 single-threaded' \
        -n 'G4 multi-threaded ({{g4_concurrency}}T)' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} --ext-str concurrency={{g4_concurrency}} workflows/gun_mt_noop.jsonnet)' \
        --export-markdown bench-geant4.md \
        --export-json bench-geant4.json

# Compare G4 with and without I/O
bench-geant4-io: build
    hyperfine \
        --warmup 1 \
        --min-runs 3 \
        --reference 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} workflows/gun_st_bench.jsonnet)' \
        --reference-name 'G4 ST noop' \
        -n 'G4 ST full' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} workflows/gun_st_full.jsonnet)' \
        --export-markdown bench-geant4-io.md

# Sweep G4 concurrency levels
bench-geant4-threads: build
    hyperfine \
        --warmup 1 \
        --min-runs 3 \
        -L concurrency 1,2,4,8 \
        -n 'G4 {concurrency}T' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} --ext-str concurrency={concurrency} workflows/gun_mt_noop.jsonnet)' \
        --export-markdown bench-geant4-threads.md \
        --export-json bench-geant4-threads.json

# Measure pure framework overhead: gun source with noop (no G4)
bench-gun-noop: build
    hyperfine \
        --warmup 1 \
        --min-runs 5 \
        --reference 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{events}} workflows/gun_noop.jsonnet)' \
        --reference-name 'gun noop (no G4)' \
        -n 'gun + G4 ST noop' 'PHLEX_PLUGIN_PATH={{plugin_path}} phlex -c <(jsonnet --ext-str events={{g4_events}} workflows/gun_st_bench.jsonnet)' \
        --export-markdown bench-gun-noop.md
