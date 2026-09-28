# Is Async Truly Zero-Cost?

**Benchmarking latency and jitter in `no_std` Rust/Embassy vs. C/FreeRTOS on ARM Cortex-M**

This repository contains the source code, raw measurements, cleaned datasets, analysis script, figures, and current paper draft for a controlled benchmark on an **STM32F411CEU6 (Black Pill, ARM Cortex-M4)**.

The experiment compares two embedded concurrency approaches on the same microcontroller:

- **Rust / Embassy**: `no_std` Rust with Embassy's async executor.
- **C / FreeRTOS**: FreeRTOS tasks using direct task notifications.

The main experiment measures timestamp overhead, CRC-32 execution, and a request/reply round trip between two concurrent execution contexts. Each implementation records **1,000 measured samples** after warm-up. Timing uses the Cortex-M DWT cycle counter. No logging occurs inside the measured sample loop.

> **Scope:** These results compare the two implementations and configurations in this repository. They do not establish that Rust is universally faster than C, or that Embassy is universally faster than FreeRTOS.

## Headline result

Both runs use a nominal **16 MHz HSI system clock**.

| Round-trip metric | Rust / Embassy | C / FreeRTOS |
|---|---:|---:|
| Samples | 1,000 | 1,000 |
| Median | **760 cycles** | **1,077 cycles** |
| Median time at 16 MHz | **47.50 us** | **67.31 us** |
| Mean | 760.00 cycles | 1,140.76 cycles |
| P95 | 760 cycles | 1,825 cycles |
| P99 | 760 cycles | 1,927 cycles |
| Maximum | 760 cycles | 1,932 cycles |

For this run, the FreeRTOS median round trip used **1.417x** as many cycles as the Rust/Embassy median. Expressed relative to the FreeRTOS median, the Rust/Embassy median was **29.43% lower**.

The Rust run was constant at 760 cycles across the captured 1,000 samples. The FreeRTOS run had a 1,077-cycle median with a higher-latency tail. See `data/summary.txt` and the generated figures for the complete descriptive statistics.

## Repository layout

```text
.
|-- rust-embassy/       Rust/Embassy firmware source
|-- freertos/           STM32CubeIDE/FreeRTOS firmware source
|-- data/
|   |-- rust.csv        Clean 1,000-sample Rust dataset
|   |-- freertos.csv    Clean 1,000-sample FreeRTOS dataset
|   |-- summary.txt     Reproduced descriptive statistics
|   `-- raw/            Original captured outputs
|-- analysis/
|   |-- analyze.py      Statistics + figure generation
|   `-- requirements.txt
|-- figures/            Generated plots
`-- paper/              Current GGSC paper draft
```

## Hardware and measurement setup

- MCU: STM32F411CEU6, ARM Cortex-M4
- Board: STM32F411 Black Pill
- Debug/programming interface: ST-Link over SWD with `probe-rs`
- System clock: HSI, 16 MHz
- AHB/APB prescalers: /1
- Timing source: DWT `CYCCNT`
- Samples: 1,000 per implementation
- Warm-up: 100 CRC and request/reply operations before collection
- CRC input: 256-byte deterministic buffer
- CRC check vector: `"123456789"` -> `0xCBF43926`
- Benchmark buffer CRC: `0x8C70078C`

The `empty` measurement records the cost of the timestamp path itself. `crc` records CRC-32 work on the 256-byte buffer. `roundtrip` records the request/reply synchronization path.

## Rust / Embassy configuration

The Rust firmware is in `rust-embassy/`.

Important pinned dependencies include Embassy Executor 0.7.0, Embassy STM32 0.2.0, and Embassy Sync 0.6.2. The release profile uses `opt-level = 3`, one codegen unit, and `panic = "abort"`.

Prerequisites:

```powershell
rustup target add thumbv7em-none-eabihf
cargo install probe-rs-tools
```

From `rust-embassy/`:

```powershell
cargo run --release
```

The `.cargo/config.toml` file selects `thumbv7em-none-eabihf` and uses:

```text
probe-rs run --chip STM32F411CEUx --protocol swd
```

A successful run emits `META,rust`, 1,000 `BENCH,rust` records, and `DONE,rust` after collection.

## C / FreeRTOS configuration

The FreeRTOS firmware is in `freertos/` and can be opened with STM32CubeIDE. The project was generated for STM32F411CEUx with FreeRTOS and then connected to the benchmark module in `Core/Src/bench.c`.

Important benchmark settings in `Core/Inc/FreeRTOSConfig.h` include:

```c
#define configUSE_PREEMPTION 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configUSE_TIME_SLICING 0
#define INCLUDE_uxTaskGetStackHighWaterMark 1
```

`MX_FREERTOS_Init()` creates the benchmark tasks before `osKernelStart()`. The benchmark tasks use static allocation and equal priority; time slicing is disabled for the controlled request/reply experiment.

The recorded FreeRTOS release build used STM32CubeIDE's **Release** configuration with `-Os`. This differs from Rust's `opt-level = 3` and is an important limitation when interpreting the CRC and code-generation results.

The resulting ELF can be programmed with `probe-rs`, for example:

```powershell
probe-rs run --chip STM32F411CEUx --protocol swd .\Release\blackpill_freertos_bench.elf
```

The benchmark stores 1,000 samples in `bench_samples` and sets `bench_done = 1` after collection. The raw run in this repository was extracted from target RAM with `probe-rs read` and converted to `data/freertos.csv`.

## Reproduce the analysis

Python 3 is sufficient for the numerical summary. Install Matplotlib only if you also want the figures:

```powershell
cd analysis
python -m pip install -r requirements.txt
python analyze.py
```

The script validates that each cleaned dataset contains exactly 1,000 samples, prints descriptive statistics, and regenerates the plots under `figures/`.

## Methodological notes and limitations

1. This is a **single-board, single-run controlled comparison** with 1,000 samples per implementation. Repeated independent runs would provide stronger evidence about run-to-run variation.
2. The two systems use different concurrency models: Embassy async tasks versus FreeRTOS preemptive tasks and direct notifications. The result therefore reflects the complete tested software paths, not language cost alone.
3. The release optimization settings are not identical (`opt-level=3` for Rust and `-Os` for the captured FreeRTOS build). This is especially relevant to the CRC result.
4. The FreeRTOS project includes the STM32 HAL/CMSIS-RTOS integration generated by STM32CubeMX. The Rust implementation uses Embassy's STM32 support and executor.
5. The experiment reports DWT cycle counts. Microsecond values are derived from the verified nominal 16 MHz clock.
6. Memory/flash footprint should be treated as a separate analysis unless both implementations are rebuilt and measured under explicitly matched inclusion rules.

## Data format

Both cleaned CSV files use:

```text
sample,empty,crc,roundtrip
```

All timing values are CPU cycles. At 16 MHz, one cycle is 0.0625 us.

## Paper

`paper/GGSC_2026_draft.docx` is the current two-column conference-paper draft. It is included for transparency but may change before submission.

## Citation

If you use this repository, please cite the accompanying paper. A machine-readable `CITATION.cff` file is included and can be updated with the final conference publication details.

## License

Original benchmark and analysis code in this repository is released under the MIT License. STM32Cube, FreeRTOS, CMSIS, Rust crates, and other third-party components remain under their respective upstream licenses and notices.
