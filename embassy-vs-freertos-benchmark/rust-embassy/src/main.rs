
#![no_std]
#![no_main]

use core::{hint::black_box, sync::atomic::{compiler_fence, Ordering}};
use defmt_rtt as _;
use panic_probe as _;
use embassy_executor::Spawner;
use embassy_sync::{blocking_mutex::raw::CriticalSectionRawMutex, signal::Signal};

const N: usize = 1000;
const WARMUP: usize = 100;
static REQUEST: Signal<CriticalSectionRawMutex, ()> = Signal::new();
static RESPONSE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

#[derive(Clone, Copy)]
struct Sample { empty: u32, crc: u32, roundtrip: u32 }

// This binary alone configures DWT/DEMCR, once, before the worker is spawned.
// Raw access is deliberately restricted to these MMIO registers; do not add
// another DWT owner or debugger trace/profiling agent that reconfigures them.
fn enable_dwt() {
    unsafe {
        let demcr = 0xE000_EDFC as *mut u32;
        let ctrl = 0xE000_1000 as *mut u32;
        let counter = 0xE000_1004 as *mut u32;
        demcr.write_volatile(demcr.read_volatile() | (1 << 24));
        ctrl.write_volatile(ctrl.read_volatile() & !1);
        counter.write_volatile(0);
        ctrl.write_volatile(ctrl.read_volatile() | 1);
    }
    cortex_m::asm::dsb();
    cortex_m::asm::isb();
    let a = stamp();
    cortex_m::asm::delay(100);
    assert!(stamp().wrapping_sub(a) > 0, "DWT counter is not running");
}

#[inline(always)]
fn stamp() -> u32 {
    compiler_fence(Ordering::SeqCst);
    cortex_m::asm::dsb();
    cortex_m::asm::isb();
    let value = cortex_m::peripheral::DWT::cycle_count();
    compiler_fence(Ordering::SeqCst);
    value
}

#[inline(never)]
fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg();
            crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
        }
    }
    !crc
}

#[embassy_executor::task]
async fn responder() {
    loop {
        REQUEST.wait().await;
        RESPONSE.signal(());
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let _p = embassy_stm32::init(Default::default());

    // STM32F411: require HSI SYSCLK, AHB /1, APB1 /1, APB2 /1.
    // Default HAL configuration is checked rather than silently assumed.
    let cfgr = unsafe { (0x4002_3808 as *const u32).read_volatile() };
    assert!(cfgr & 0xFCFC == 0, "Use HSI at 16 MHz with bus dividers /1");
    let acr = unsafe { (0x4002_3C00 as *const u32).read_volatile() };
    defmt::info!("META,rust,hclk_nominal_hz=16000000,rcc_cfgr={},flash_acr={}", cfgr, acr);

    enable_dwt();
    // These arrays live in static RAM, not inside the task arena or stack.
    let samples = cortex_m::singleton!(: [Sample; N] =
        [Sample { empty: 0, crc: 0, roundtrip: 0 }; N]).unwrap();
    let data = cortex_m::singleton!(: [u8; 256] = [0; 256]).unwrap();
    for (i, byte) in data.iter_mut().enumerate() {
        *byte = (i as u8).wrapping_mul(17).wrapping_add(3);
    }
    assert_eq!(crc32(black_box(b"123456789")), 0xCBF4_3926);
    let expected = crc32(black_box(&data[..]));
    assert_eq!(expected, 0x8C70_078C);
    spawner.spawn(responder()).unwrap();

    // First await lets the newly spawned worker run. All warm-up records
    // are discarded. There is exactly one outstanding request at a time.
    for _ in 0..WARMUP {
        black_box(crc32(black_box(&data[..])));
        REQUEST.signal(());
        RESPONSE.wait().await;
    }

    for sample in samples.iter_mut() {
        let start = stamp();
        sample.empty = stamp().wrapping_sub(start);

        let start = stamp();
        let result = black_box(crc32(black_box(&data[..])));
        sample.crc = stamp().wrapping_sub(start);
        assert_eq!(result, expected); // Outside measured interval.

        let start = stamp();
        REQUEST.signal(());
        RESPONSE.wait().await;
        sample.roundtrip = stamp().wrapping_sub(start);
    }

    // No logging took place inside or between collected samples.
    defmt::info!("META,rust,crc_result={},samples={}", expected, N);
    for (i, sample) in samples.iter().enumerate() {
        defmt::info!("BENCH,rust,{},{},{},{}", i, sample.empty, sample.crc, sample.roundtrip);
    }
    defmt::info!("DONE,rust");
    core::future::pending::<()>().await;
}
