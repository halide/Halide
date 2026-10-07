use std::ops::Range;
use std::vec;

use ::colorous;
use serde::{Deserialize, Serialize};

use crate::colormap::{Colormap, METRIC_PALETTE};
use crate::trace::{for_each_lane_element, FuncGeometry, Trace, TracePacket};

#[derive(Deserialize, Clone, Copy)]
pub enum NormalizationMode {
    #[serde(rename = "Across Funcs")]
    AcrossFuncs,
    #[serde(rename = "Per Func")]
    PerFunc,
}

// A trait that all 2D Canvas renderers implement.
pub trait Renderer: Sized {
    type Value;

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize);
    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8>;
    fn to_nan_overlay(&self) -> Vec<u8>;
    fn to_inf_overlay(&self) -> Vec<u8>;
    fn to_values(&self) -> Vec<Self::Value>;
}

/// Packs a per-pixel predicate over channel values into a 1-bit-per-pixel mask: bit 1 for a pixel
/// where `is_set` holds for any channel, bit 0 otherwise. `plane` holds one run of `pixels` values
/// per channel. Bits are packed MSB-first within each byte, in row-major pixel order; the final
/// byte is zero-padded if the pixel count isn't a multiple of 8. The frontend expands this back
/// into a colored RGBA8 overlay, since every set pixel shares the same user-selected overlay color.
fn pack_mask<T: Copy>(plane: &[T], pixels: usize, is_set: impl Fn(T) -> bool) -> Vec<u8> {
    pack_bits((0..pixels).map(|p| plane[p..].iter().step_by(pixels).any(|&v| is_set(v))))
}

fn pack_bits(bits: impl ExactSizeIterator<Item = bool>) -> Vec<u8> {
    let mut out = vec![0u8; bits.len().div_ceil(8)];
    for (i, bit) in bits.enumerate() {
        if bit {
            out[i / 8] |= 1 << (7 - (i % 8));
        }
    }
    out
}

fn nan_mask(plane: &[f64], pixels: usize) -> Vec<u8> {
    pack_mask(plane, pixels, f64::is_nan)
}

fn inf_mask(plane: &[f64], pixels: usize) -> Vec<u8> {
    pack_mask(plane, pixels, f64::is_infinite)
}

/// The displayed part of a Func's elements (see `FuncGeometry`): all of dims 0 and 1 and of dims 2
/// up to `first_slice_dim`, with the dims from `first_slice_dim` up fixed by the slice.
struct Plane {
    first_slice_dim: usize,
    /// The coordinates of logical dims 2 and up, clamped to the Func.
    coords: Vec<i32>,
    offset: usize,
    len: usize,
}

impl Plane {
    fn new(geom: &FuncGeometry, first_slice_dim: usize) -> Self {
        let kept = (first_slice_dim - 2).min(geom.extents.len());
        Self {
            first_slice_dim,
            coords: geom.mins.clone(),
            offset: 0,
            len: geom.pixels() * geom.extents[..kept].iter().product::<usize>(),
        }
    }

    /// The number of dims from 2 up that are displayed whole.
    fn kept(&self) -> usize {
        (self.first_slice_dim - 2).min(self.coords.len())
    }

    /// `slice` holds the coordinates of logical dims 2 and up. Those below `first_slice_dim` are
    /// ignored, and missing ones are taken to be at their min.
    fn set_slice(&mut self, geom: &FuncGeometry, slice: &[i32]) {
        let kept = self.kept();
        let mut index = 0;
        for d in (0..geom.extents.len()).rev() {
            let (min, extent) = (geom.mins[d], geom.extents[d]);
            let c = slice
                .get(d)
                .map_or(0, |&c| (c - min).clamp(0, extent as i32 - 1));
            self.coords[d] = min + c;
            if d >= kept {
                index = index * extent + c as usize;
            }
        }
        self.offset = index * self.len;
    }

    fn range(&self) -> Range<usize> {
        self.offset..self.offset + self.len
    }
}

// ── Value rendering (Grayscale / RGB) ────────────────────────────────────────────────────────────

/// The raw values of a Func, displayed one 2D slice at a time, shared by the grayscale and RGB
/// renderers. In color, dim 2 is the channel axis and the slice fixes dims 3 and up; otherwise the
/// slice fixes dims 2 and up. Every element is kept, and values are mapped to display levels at
/// render time, so neither changing the slice nor moving the black / white point requires a
/// re-seek.
struct ValueSlice {
    geom: FuncGeometry,
    plane: Plane,
    min_v: f64,
    max_v: f64,
    black: f64,
    white: f64,
    values: Vec<f64>,
    written: Vec<bool>,
    applied_k: usize,
}

impl ValueSlice {
    fn new(trace: &Trace, func: &str, color: bool) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let stats = trace.funcs.get(func)?;
        let min_v = stats.min_value.unwrap_or(0.0);
        let max_v = stats.max_value.unwrap_or(255.0);
        let plane = Plane::new(&geom, if color { 3 } else { 2 });
        let len = geom.num_elements();

        Some(Self {
            geom,
            plane,
            min_v,
            max_v,
            black: min_v,
            white: max_v,
            values: vec![0.0; len],
            written: vec![false; len],
            applied_k: 0,
        })
    }

    fn channels(&self) -> usize {
        self.plane.len / self.geom.pixels()
    }

    /// The values of the displayed slice, one run of pixels per channel.
    fn plane_values(&self) -> &[f64] {
        &self.values[self.plane.range()]
    }

    fn reset(&mut self) {
        self.values.iter_mut().for_each(|v| *v = 0.0);
        self.written.iter_mut().for_each(|w| *w = false);
        self.applied_k = 0;
    }

    /// See `Plane::set_slice`.
    fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn set_view(&mut self, slice: &[i32], black: f64, white: f64) {
        self.set_slice(slice);
        self.black = black;
        self.white = white;
    }

    fn apply_packet(&mut self, pkt: &TracePacket) {
        let Self {
            geom,
            values,
            written,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |lane, i| {
            if let Some(v) = pkt.decoded_value(lane) {
                values[i] = v;
                written[i] = true;
            }
        });
    }

    /// Applies the first `target_k` of `value_indices` (see `Trace::func_value_indices`).
    fn seek(&mut self, trace: &Trace, value_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(value_indices.len());
        if target_k < self.applied_k {
            self.reset();
        }

        for &global_idx in &value_indices[self.applied_k..target_k] {
            self.apply_packet(&trace.packets[global_idx]);
        }

        self.applied_k = target_k;
    }

    /// The display level of the value at index `i` of the displayed slice: 0 at the black point,
    /// 255 at the white point. Never-written locations are black.
    #[inline]
    fn level(&self, i: usize) -> u8 {
        let i = self.plane.offset + i;
        if !self.written[i] {
            return 0;
        }
        let v = self.values[i];
        (255.0 * (v - self.black) / (self.white - self.black)).clamp(0.0, 255.0) as u8
    }

    /// Bins `v` into one of `NUM_HISTOGRAM_BINS` fixed-width buckets spanning `[min_v, max_v]`.
    #[inline]
    fn histogram_bucket(&self, v: f64) -> usize {
        let range = self.max_v - self.min_v;
        let bucket = if range > 0.0 {
            (((v - self.min_v) / range) * NUM_HISTOGRAM_BINS as f64) as usize
        } else {
            0
        };
        bucket.min(NUM_HISTOGRAM_BINS - 1)
    }

    /// The coordinates and per-channel values at pixel (`px`, `py`) of the slice. In color, the
    /// channel dim's coordinate is `None`. Values are `None` where never written.
    fn probe(&self, px: usize, py: usize) -> Option<Probe> {
        let FuncGeometry {
            width,
            height,
            min_x,
            min_y,
            ..
        } = self.geom;
        if px >= width || py >= height {
            return None;
        }
        let kept = self.plane.kept();
        let mut coords = vec![Some(min_x + px as i32), Some(min_y + py as i32)];
        coords.extend(
            self.plane
                .coords
                .iter()
                .enumerate()
                .map(|(d, &c)| (d >= kept).then_some(c)),
        );
        let pixels = self.geom.pixels();
        let base = self.plane.offset + py * width + px;
        let values = (0..self.channels())
            .map(|c| base + c * pixels)
            .map(|i| self.written[i].then(|| self.values[i].to_string()))
            .collect();
        Some(Probe { coords, values })
    }

    fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(self.plane_values(), self.geom.pixels())
    }

    fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(self.plane_values(), self.geom.pixels())
    }

    /// A mask (see `pack_mask`) of the pixels with any channel written.
    fn to_written_mask(&self) -> Vec<u8> {
        pack_mask(&self.written[self.plane.range()], self.geom.pixels(), |w| w)
    }
}

const NUM_HISTOGRAM_BINS: usize = 256;

/// The value(s) of a Func at one displayed pixel. See `ValueSlice::probe`.
#[derive(Serialize)]
pub struct Probe {
    coords: Vec<Option<i32>>,
    values: Vec<Option<String>>,
}

pub struct GrayscaleState(ValueSlice);

impl GrayscaleState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        ValueSlice::new(trace, func, false).map(Self)
    }

    pub fn set_view(&mut self, slice: &[i32], black: f64, white: f64) {
        self.0.set_view(slice, black, white);
    }

    pub fn set_slice(&mut self, slice: &[i32]) {
        self.0.set_slice(slice);
    }

    pub fn probe(&self, px: usize, py: usize) -> Option<Probe> {
        self.0.probe(px, py)
    }

    pub fn to_written_mask(&self) -> Vec<u8> {
        self.0.to_written_mask()
    }

    /// Bins the raw (pre-normalization) values into 256 fixed-width buckets (one per displayable
    /// 8-bit gray level) spanning the Func's `[min_v, max_v]`.
    pub fn to_histogram(&self) -> Vec<u32> {
        let mut bins = vec![0u32; NUM_HISTOGRAM_BINS];
        for &v in self.0.plane_values() {
            bins[self.0.histogram_bucket(v)] += 1;
        }
        bins
    }
}

impl Renderer for GrayscaleState {
    type Value = f64;

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        self.0.seek(trace, store_indices, target_k);
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let mut out = vec![0u8; self.0.plane.len * 4];
        for (i, chunk) in out.chunks_exact_mut(4).enumerate() {
            let gray = self.0.level(i);
            chunk[0] = gray;
            chunk[1] = gray;
            chunk[2] = gray;
            chunk[3] = 255;
        }
        out
    }

    fn to_values(&self) -> Vec<f64> {
        self.0.plane_values().to_vec()
    }

    fn to_nan_overlay(&self) -> Vec<u8> {
        self.0.to_nan_overlay()
    }

    fn to_inf_overlay(&self) -> Vec<u8> {
        self.0.to_inf_overlay()
    }
}

pub struct RgbState(ValueSlice);

impl RgbState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        ValueSlice::new(trace, func, true).map(Self)
    }

    pub fn set_view(&mut self, slice: &[i32], black: f64, white: f64) {
        self.0.set_view(slice, black, white);
    }

    pub fn set_slice(&mut self, slice: &[i32]) {
        self.0.set_slice(slice);
    }

    pub fn probe(&self, px: usize, py: usize) -> Option<Probe> {
        self.0.probe(px, py)
    }

    /// Bins the raw (pre-normalization) per-channel values into 256 fixed-width buckets spanning
    /// the Func's `[min_v, max_v]`. When `channels >= 3`, returns three histograms back to back (R,
    /// then G, then B, 256 `u32`s each — the caller recovers the channel count as `len / 256`).
    /// Otherwise falls back to a single histogram over channel 0.
    pub fn to_histogram(&self) -> Vec<u32> {
        let channels = self.0.channels();
        let pixels = self.0.geom.pixels();
        let plane = self.0.plane_values();
        let counted = if channels < 3 { 1 } else { 3 };

        let mut bins = vec![0u32; NUM_HISTOGRAM_BINS * counted];
        for (c, values) in plane.chunks_exact(pixels).take(counted).enumerate() {
            for &v in values {
                bins[c * NUM_HISTOGRAM_BINS + self.0.histogram_bucket(v)] += 1;
            }
        }
        bins
    }
}

impl Renderer for RgbState {
    type Value = f64;

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        self.0.seek(trace, store_indices, target_k);
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let channels = self.0.channels();
        let pixels = self.0.geom.pixels();
        let mut out = vec![0u8; pixels * 4];
        for (p, chunk) in out.chunks_exact_mut(4).enumerate() {
            if channels >= 3 {
                chunk[0] = self.0.level(p);
                chunk[1] = self.0.level(p + pixels);
                chunk[2] = self.0.level(p + 2 * pixels);
            } else {
                let v = self.0.level(p);
                chunk[0] = v;
                chunk[1] = v;
                chunk[2] = v;
            }
            chunk[3] = 255;
        }
        out
    }

    /// The values of the displayed slice, with channels interleaved.
    fn to_values(&self) -> Vec<f64> {
        let channels = self.0.channels();
        let pixels = self.0.geom.pixels();
        let plane = self.0.plane_values();
        (0..pixels)
            .flat_map(|p| (0..channels).map(move |c| plane[c * pixels + p]))
            .collect()
    }

    fn to_nan_overlay(&self) -> Vec<u8> {
        self.0.to_nan_overlay()
    }

    fn to_inf_overlay(&self) -> Vec<u8> {
        self.0.to_inf_overlay()
    }
}

// ── Store frequency rendering ────────────────────────────────────────────────────────────────────

pub struct StoreFrequencyState {
    geom: FuncGeometry,
    plane: Plane,
    counts: Vec<u32>,
    values: Vec<f64>,
    local_max_store_count: u32,
    global_max_store_count: u32,
    applied_k: usize,
}

impl StoreFrequencyState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let plane = Plane::new(&geom, 2);
        let counts = vec![0u32; geom.num_elements()];
        let values = vec![0f64; geom.num_elements()];

        let local_max_store_count = trace.funcs.get(func).map_or(0, |s| s.max_store_count);
        let global_max_store_count = trace
            .funcs
            .values()
            .map(|s| s.max_store_count)
            .max()
            .unwrap_or(0);

        Some(Self {
            geom,
            plane,
            counts,
            values,
            local_max_store_count,
            global_max_store_count,
            applied_k: 0,
        })
    }

    /// See `Plane::set_slice`.
    pub fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn increment(&mut self, pkt: &TracePacket) {
        let Self {
            geom,
            counts,
            values,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |lane, i| {
            counts[i] += 1;
            if let Some(v) = pkt.decoded_value(lane) {
                values[i] = v;
            }
        });
    }

    pub fn to_tabular_data(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_store_count,
            NormalizationMode::PerFunc => self.local_max_store_count,
        };

        let exceeds_max_bins = max > 64;
        // Pre-allocate tabular_data, capping to 64 bins.
        let mut tabular_data = vec![
            0u32;
            if exceeds_max_bins {
                64
            } else {
                max as usize + 1
            }
        ];

        for &c in &self.counts[self.plane.range()] {
            let bucket = if exceeds_max_bins { c * 63 / max } else { c };

            tabular_data[bucket.clamp(0, 63) as usize] += 1;
        }

        tabular_data
    }
}

impl Renderer for StoreFrequencyState {
    type Value = u32;

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.values.iter_mut().for_each(|v| *v = 0.0);
            self.applied_k = 0;
        }
        for &idx in &store_indices[self.applied_k..target_k] {
            self.increment(&trace.packets[idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let lut = Colormap::from_hex(&METRIC_PALETTE).to_lut();

        let scale = match (normalization_mode, self.global_max_store_count) {
            (NormalizationMode::AcrossFuncs, 0) => 0.0,
            (NormalizationMode::AcrossFuncs, global_max) => 255.0 / global_max as f64,
            (NormalizationMode::PerFunc, _) => {
                let local_max = self.local_max_store_count;
                if local_max > 0 {
                    255.0 / local_max as f64
                } else {
                    0.0
                }
            }
        };

        let mut out = vec![0u8; self.plane.len * 4];
        for (chunk, &count) in out
            .chunks_exact_mut(4)
            .zip(&self.counts[self.plane.range()])
        {
            let ti = (count as f64 * scale) as usize;
            let [r, g, b] = lut[ti.min(255)];
            chunk[0] = r;
            chunk[1] = g;
            chunk[2] = b;
            chunk[3] = 255;
        }
        out
    }

    fn to_values(&self) -> Vec<u32> {
        self.counts[self.plane.range()].to_vec()
    }

    fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(&self.values[self.plane.range()], self.plane.len)
    }

    fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(&self.values[self.plane.range()], self.plane.len)
    }
}

// ── Load frequency rendering ─────────────────────────────────────────────────────────────────────

pub struct LoadFrequencyState {
    geom: FuncGeometry,
    plane: Plane,
    counts: Vec<u32>,
    values: Vec<f64>,
    local_max_load_count: u32,
    global_max_load_count: u32,
    applied_k: usize,
}

impl LoadFrequencyState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let plane = Plane::new(&geom, 2);
        let counts = vec![0u32; geom.num_elements()];
        let values = vec![0f64; geom.num_elements()];

        let local_max_load_count = trace.funcs.get(func).map_or(0, |s| s.max_load_count);
        let global_max_load_count = trace
            .funcs
            .values()
            .map(|s| s.max_load_count)
            .max()
            .unwrap_or(0);

        Some(Self {
            geom,
            plane,
            counts,
            values,
            local_max_load_count,
            global_max_load_count,
            applied_k: 0,
        })
    }

    /// See `Plane::set_slice`.
    pub fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn increment(&mut self, pkt: &TracePacket) {
        let Self {
            geom,
            counts,
            values,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |lane, i| {
            counts[i] += 1;
            if let Some(v) = pkt.decoded_value(lane) {
                values[i] = v;
            }
        });
    }

    pub fn to_tabular_data(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_load_count,
            NormalizationMode::PerFunc => self.local_max_load_count,
        };

        let exceeds_max_bins = max > 64;
        // Pre-allocate tabular_data, capping to 64 bins.
        let mut tabular_data = vec![
            0u32;
            if exceeds_max_bins {
                64
            } else {
                max as usize + 1
            }
        ];

        for &c in &self.counts[self.plane.range()] {
            let bucket = if exceeds_max_bins { c * 63 / max } else { c };

            tabular_data[bucket.clamp(0, 63) as usize] += 1;
        }

        tabular_data
    }
}

impl Renderer for LoadFrequencyState {
    type Value = u32;

    fn seek(&mut self, trace: &Trace, load_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(load_indices.len());
        if target_k < self.applied_k {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.values.iter_mut().for_each(|v| *v = 0.0);
            self.applied_k = 0;
        }
        for &idx in &load_indices[self.applied_k..target_k] {
            self.increment(&trace.packets[idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let lut = Colormap::from_hex(&METRIC_PALETTE).to_lut();

        let scale = match (normalization_mode, self.global_max_load_count) {
            (NormalizationMode::AcrossFuncs, 0) => 0.0,
            (NormalizationMode::AcrossFuncs, global_max) => 255.0 / global_max as f64,
            (NormalizationMode::PerFunc, _) => {
                if self.local_max_load_count > 0 {
                    255.0 / self.local_max_load_count as f64
                } else {
                    0.0
                }
            }
        };

        let mut out = vec![0u8; self.plane.len * 4];
        for (chunk, &count) in out
            .chunks_exact_mut(4)
            .zip(&self.counts[self.plane.range()])
        {
            let ti = (count as f64 * scale) as usize;
            let [r, g, b] = lut[ti.min(255)];
            chunk[0] = r;
            chunk[1] = g;
            chunk[2] = b;
            chunk[3] = 255;
        }
        out
    }

    fn to_values(&self) -> Vec<u32> {
        self.counts[self.plane.range()].to_vec()
    }

    fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(&self.values[self.plane.range()], self.plane.len)
    }

    fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(&self.values[self.plane.range()], self.plane.len)
    }
}

// ── Redundant store rendering ────────────────────────────────────────────────────────────────────

pub struct RedundantState {
    geom: FuncGeometry,
    plane: Plane,
    last_values: Vec<Option<u64>>,
    redundant_store_counts: Vec<u32>,
    local_max_redundant_store_count: u32,
    global_max_redundant_store_count: u32,
    applied_store_k: usize,
    applied_load_k: usize,
}

impl RedundantState {
    /// Builds an empty redundant state for `func`, or `None` if the Func has no usable geometry.
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let plane = Plane::new(&geom, 2);

        let local_max_redundant_store_count =
            trace.funcs.get(func).map(|s| s.max_redundant_store_count)?;
        let global_max_redundant_store_count = trace
            .funcs
            .values()
            .map(|s| s.max_redundant_store_count)
            .max()?;

        Some(Self {
            last_values: vec![None; geom.num_elements()],
            redundant_store_counts: vec![0u32; geom.num_elements()],
            geom,
            plane,
            local_max_redundant_store_count,
            global_max_redundant_store_count,
            applied_store_k: 0,
            applied_load_k: 0,
        })
    }

    fn reset(&mut self) {
        self.last_values.iter_mut().for_each(|v| *v = None);
        self.redundant_store_counts.iter_mut().for_each(|c| *c = 0);
        self.applied_store_k = 0;
        self.applied_load_k = 0;
    }

    /// See `Plane::set_slice`.
    pub fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let Self {
            geom,
            last_values,
            redundant_store_counts,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |lane, i| {
            let Some(v) = pkt.decoded_value(lane) else {
                return;
            };
            let v_bits = v.to_bits();
            if last_values[i] == Some(v_bits) {
                redundant_store_counts[i] += 1;
            }
            last_values[i] = Some(v_bits);
        });
    }

    /// A load observes the current value at a location, so it breaks the redundancy chain: clear
    /// the last-written value there so a subsequent store is never counted as redundant against a
    /// value that predates the load.
    fn apply_load(&mut self, pkt: &TracePacket) {
        let Self {
            geom, last_values, ..
        } = self;
        for_each_lane_element(pkt, geom, |_lane, i| {
            last_values[i] = None;
        });
    }

    /// Seeks to the state after the first `target_store_k` stores and `target_load_k` loads.
    /// Events are replayed in global packet order via a two-pointer merge of the two sorted index
    /// lists. Backward seeks (either counter regresses) reset and replay from zero.
    pub fn seek(
        &mut self,
        trace: &Trace,
        store_indices: &[usize],
        load_indices: &[usize],
        target_store_k: usize,
        target_load_k: usize,
    ) {
        let target_store_k = target_store_k.min(store_indices.len());
        let target_load_k = target_load_k.min(load_indices.len());

        if target_store_k < self.applied_store_k || target_load_k < self.applied_load_k {
            self.reset();
        }

        let store_slice = &store_indices[self.applied_store_k..target_store_k];
        let load_slice = &load_indices[self.applied_load_k..target_load_k];
        let mut si = 0;
        let mut li = 0;

        while si < store_slice.len() || li < load_slice.len() {
            let next_is_store = si < store_slice.len()
                && (li >= load_slice.len() || store_slice[si] < load_slice[li]);

            if next_is_store {
                self.apply_store(&trace.packets[store_slice[si]]);
                si += 1;
            } else {
                self.apply_load(&trace.packets[load_slice[li]]);
                li += 1;
            }
        }

        self.applied_store_k = target_store_k;
        self.applied_load_k = target_load_k;
    }

    pub fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let lut = Colormap::from_hex(&METRIC_PALETTE).to_lut();

        let scale = match (normalization_mode, self.global_max_redundant_store_count) {
            (NormalizationMode::AcrossFuncs, 0) => 0.0,
            (NormalizationMode::AcrossFuncs, global_max) => 255.0 / global_max as f64,
            (NormalizationMode::PerFunc, _) => {
                if self.local_max_redundant_store_count > 0 {
                    255.0 / self.local_max_redundant_store_count as f64
                } else {
                    0.0
                }
            }
        };

        let mut out = vec![0u8; self.plane.len * 4];
        for (chunk, &count) in out
            .chunks_exact_mut(4)
            .zip(&self.redundant_store_counts[self.plane.range()])
        {
            let ti = (count as f64 * scale) as usize;
            let [r, g, b] = lut[ti.min(255)];
            chunk[0] = r;
            chunk[1] = g;
            chunk[2] = b;
            chunk[3] = 255;
        }
        out
    }

    pub fn to_tabular_data(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_redundant_store_count,
            NormalizationMode::PerFunc => self.local_max_redundant_store_count,
        };

        let exceeds_max_bins = max > 64;
        // Pre-allocate tabular_data, capping to 64 bins.
        let mut tabular_data = vec![
            0u32;
            if exceeds_max_bins {
                64
            } else {
                max as usize + 1
            }
        ];

        for &c in &self.redundant_store_counts[self.plane.range()] {
            let bucket = if exceeds_max_bins { c * 63 / max } else { c };

            tabular_data[bucket.clamp(0, 63) as usize] += 1;
        }

        tabular_data
    }

    pub fn to_values(&self) -> Vec<u32> {
        self.redundant_store_counts[self.plane.range()].to_vec()
    }

    fn plane_values(&self) -> Vec<f64> {
        self.last_values[self.plane.range()]
            .iter()
            .map(|v| v.map_or(0.0, f64::from_bits))
            .collect()
    }

    pub fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(&self.plane_values(), self.plane.len)
    }

    pub fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(&self.plane_values(), self.plane.len)
    }
}

// ── Reuse distance rendering ─────────────────────────────────────────────────────────────────────

/// Per-pixel maximum reuse distance for one Func, seekable along the global timeline.
///
/// For intermediate Funcs (those with stores) the anchor is the most recent store per
/// location; reuse distance is measured to the next load from the same location.
///
/// For pipeline inputs (loads only, no stores) the anchor is the *first* load per location —
/// treating that load as a free "memcpy" — and subsequent loads measure distance from it.
///
/// Both the store and load index lists are merged in global order during seeking.
/// Backward seeks reset and replay from zero.
pub struct ReuseDistanceState {
    geom: FuncGeometry,
    plane: Plane,
    is_input: bool,
    /// Per-element anchor (see `FuncGeometry`).
    /// For intermediate Funcs: global index of the most recent store (`usize::MAX` = none yet).
    /// For inputs: global index of the first load (`usize::MAX` = none yet).
    anchor_at: Vec<usize>,
    /// Maximum observed reuse distance per element.
    max_reuse_distance: Vec<u64>,
    local_max_reuse_distance: u64,
    global_max_reuse_distance: u64,
    applied_store_k: usize,
    applied_load_k: usize,
    values: Vec<f64>,
}

impl ReuseDistanceState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let plane = Plane::new(&geom, 2);
        let n_cells = geom.num_elements();
        let is_input = trace.func_store_indices(func).is_none_or(|s| s.is_empty());

        let local_max_reuse_distance = trace.funcs.get(func).map(|s| s.max_reuse_distance)?;
        let global_max_reuse_distance = trace.funcs.values().map(|s| s.max_reuse_distance).max()?;

        Some(Self {
            geom,
            plane,
            is_input,
            anchor_at: vec![usize::MAX; n_cells],
            max_reuse_distance: vec![0u64; n_cells],
            local_max_reuse_distance,
            global_max_reuse_distance,
            applied_store_k: 0,
            applied_load_k: 0,
            values: vec![0f64; n_cells],
        })
    }

    /// See `Plane::set_slice`.
    pub fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn reset(&mut self) {
        self.anchor_at.iter_mut().for_each(|v| *v = usize::MAX);
        self.max_reuse_distance.iter_mut().for_each(|d| *d = 0);
        self.values.iter_mut().for_each(|v| *v = 0.0);
        self.applied_store_k = 0;
        self.applied_load_k = 0;
    }

    /// Seeks to the state after the first `target_store_k` stores and `target_load_k` loads.
    /// Events are replayed in global packet order via a two-pointer merge of the two sorted index
    /// lists. Backward seeks (either counter regresses) reset and replay from zero.
    pub fn seek(
        &mut self,
        trace: &Trace,
        store_indices: &[usize],
        load_indices: &[usize],
        target_store_k: usize,
        target_load_k: usize,
    ) {
        let target_store_k = target_store_k.min(store_indices.len());
        let target_load_k = target_load_k.min(load_indices.len());

        if target_store_k < self.applied_store_k || target_load_k < self.applied_load_k {
            self.reset();
        }

        let store_slice = &store_indices[self.applied_store_k..target_store_k];
        let load_slice = &load_indices[self.applied_load_k..target_load_k];
        let mut si = 0;
        let mut li = 0;

        while si < store_slice.len() || li < load_slice.len() {
            let next_is_store = si < store_slice.len()
                && (li >= load_slice.len() || store_slice[si] < load_slice[li]);

            if next_is_store {
                self.apply_store(&trace.packets[store_slice[si]], store_slice[si]);
                si += 1;
            } else {
                self.apply_load(&trace.packets[load_slice[li]], load_slice[li]);
                li += 1;
            }
        }

        self.applied_store_k = target_store_k;
        self.applied_load_k = target_load_k;
    }

    fn apply_store(&mut self, pkt: &TracePacket, global_idx: usize) {
        // Pipeline inputs have no stores; skip to avoid a stale anchor being set.
        if self.is_input {
            return;
        }

        let Self {
            geom,
            anchor_at,
            values,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |lane, i| {
            anchor_at[i] = global_idx;
            if let Some(v) = pkt.decoded_value(lane) {
                values[i] = v;
            }
        });
    }

    fn apply_load(&mut self, pkt: &TracePacket, global_idx: usize) {
        let Self {
            geom,
            is_input,
            anchor_at,
            max_reuse_distance,
            ..
        } = self;
        for_each_lane_element(pkt, geom, |_lane, i| {
            if anchor_at[i] == usize::MAX {
                // For an input, the first load is the free memcpy: it establishes the anchor and
                // records no distance.
                if *is_input {
                    anchor_at[i] = global_idx;
                }
            } else {
                let dist = (global_idx - anchor_at[i]) as u64;
                max_reuse_distance[i] = max_reuse_distance[i].max(dist);
            }
        });
    }

    pub fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let lut = Colormap::from_hex(&METRIC_PALETTE).to_lut();

        let scale = match (normalization_mode, self.global_max_reuse_distance) {
            (NormalizationMode::AcrossFuncs, 0) => 0.0,
            (NormalizationMode::AcrossFuncs, global_max) => 255.0 / global_max as f64,
            (NormalizationMode::PerFunc, _) => {
                if self.local_max_reuse_distance > 0 {
                    255.0 / self.local_max_reuse_distance as f64
                } else {
                    0.0
                }
            }
        };

        let mut out = vec![0u8; self.plane.len * 4];
        for (chunk, &dist) in out
            .chunks_exact_mut(4)
            .zip(&self.max_reuse_distance[self.plane.range()])
        {
            if dist > 0 {
                let ti = (dist as f64 * scale) as usize;
                let [r, g, b] = lut[ti.min(255)];
                chunk[0] = r;
                chunk[1] = g;
                chunk[2] = b;
            }
            chunk[3] = 255;
        }
        out
    }

    pub fn to_tabular_data(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_reuse_distance,
            NormalizationMode::PerFunc => self.local_max_reuse_distance,
        };

        let mut tabular_data = vec![0u32; 64];
        if max > 0 {
            for &dist in &self.max_reuse_distance[self.plane.range()] {
                if dist > 0 {
                    let bucket = ((dist as f64 / max as f64) * 63.0) as usize;
                    tabular_data[bucket.min(63)] += 1;
                }
            }
        }

        tabular_data
    }

    pub fn to_values(&self) -> Vec<u64> {
        self.max_reuse_distance[self.plane.range()].to_vec()
    }

    pub fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(&self.values[self.plane.range()], self.plane.len)
    }

    pub fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(&self.values[self.plane.range()], self.plane.len)
    }
}

// ── Thread Rendering ─────────────────────────────────────────────────────────────────────────────

#[derive(Deserialize, Clone, Copy, PartialEq)]
pub enum ThreadOpMode {
    Store,
    Load,
}

pub struct ThreadState {
    geom: FuncGeometry,
    plane: Plane,
    thread_ids: Vec<i32>,
    thread_id_buffer: Vec<i32>,
    global_thread_ids: Vec<i32>,
    store_counts: Vec<u32>,
    load_counts: Vec<u32>,
    applied_store_k: usize,
    applied_load_k: usize,
    applied_op_mode: Option<ThreadOpMode>,
    values: Vec<f64>,
}

impl ThreadState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let thread_ids = trace
            .func_thread_ids(func)
            .map(|ids| ids.iter().copied().collect::<Vec<i32>>())
            .unwrap_or_else(|| vec![0]);
        let n_threads = thread_ids.len();

        let global_thread_ids: Vec<i32> = trace.global_thread_ids.iter().copied().collect();

        // `-1` marks an element no store/load has touched yet.
        let thread_id_buffer = vec![-1; geom.num_elements()];

        Some(Self {
            plane: Plane::new(&geom, 2),
            values: vec![0f64; geom.num_elements()],
            geom,
            thread_ids,
            thread_id_buffer,
            global_thread_ids,
            store_counts: vec![0u32; n_threads],
            load_counts: vec![0u32; n_threads],
            applied_store_k: 0,
            applied_load_k: 0,
            applied_op_mode: None,
        })
    }

    fn reset(&mut self) {
        self.thread_id_buffer.iter_mut().for_each(|v| *v = -1);
        self.store_counts.iter_mut().for_each(|c| *c = 0);
        self.load_counts.iter_mut().for_each(|c| *c = 0);
        self.values.iter_mut().for_each(|v| *v = 0.0);
        self.applied_store_k = 0;
        self.applied_load_k = 0;
    }

    /// See `Plane::set_slice`.
    pub fn set_slice(&mut self, slice: &[i32]) {
        self.plane.set_slice(&self.geom, slice);
    }

    fn apply(&mut self, pkt: &TracePacket, op_mode: ThreadOpMode) {
        let Self {
            geom,
            thread_ids,
            thread_id_buffer,
            store_counts,
            load_counts,
            values,
            ..
        } = self;
        let thread_idx = thread_ids.binary_search(&pkt.thread_id).ok();
        let counts = match op_mode {
            ThreadOpMode::Store => store_counts,
            ThreadOpMode::Load => load_counts,
        };

        for_each_lane_element(pkt, geom, |lane, i| {
            if let Some(v) = pkt.decoded_value(lane) {
                values[i] = v;
            }
            thread_id_buffer[i] = pkt.thread_id;
            if let Some(t) = thread_idx {
                counts[t] += 1;
            }
        });
    }

    pub fn seek(
        &mut self,
        trace: &Trace,
        store_indices: &[usize],
        load_indices: &[usize],
        target_store_k: usize,
        target_load_k: usize,
        op_mode: ThreadOpMode,
    ) {
        let target_store_k = target_store_k.min(store_indices.len());
        let target_load_k = target_load_k.min(load_indices.len());

        if target_store_k < self.applied_store_k
            || target_load_k < self.applied_load_k
            || self.applied_op_mode != Some(op_mode)
        {
            self.reset();
        }

        let store_slice = &store_indices[self.applied_store_k..target_store_k];
        let load_slice = &load_indices[self.applied_load_k..target_load_k];
        let mut si = 0;
        let mut li = 0;

        while si < store_slice.len() || li < load_slice.len() {
            let next_is_store = si < store_slice.len()
                && (li >= load_slice.len() || store_slice[si] < load_slice[li]);

            match (&op_mode, next_is_store) {
                (ThreadOpMode::Store, true) => {
                    self.apply(&trace.packets[store_slice[si]], op_mode);
                    si += 1;
                }
                (ThreadOpMode::Load, false) => {
                    self.apply(&trace.packets[load_slice[li]], op_mode);
                    li += 1;
                }
                (_, true) => {
                    // Increment si even if we don't apply the store, to keep the merge moving forward.
                    si += 1;
                }
                (_, false) => {
                    // Increment li even if we don't apply the load, to keep the merge moving forward.
                    li += 1;
                }
            }
        }

        self.applied_store_k = target_store_k;
        self.applied_load_k = target_load_k;
        self.applied_op_mode = Some(op_mode);
    }

    pub fn to_rgba(&self, thread_id_filter: String) -> Vec<u8> {
        let mut out = vec![0u8; self.plane.len * 4];
        let filter_id: Option<i32> = thread_id_filter.parse::<i32>().ok();

        for (chunk, &thread_id) in out
            .chunks_exact_mut(4)
            .zip(&self.thread_id_buffer[self.plane.range()])
        {
            let color = self
                .global_thread_ids
                .binary_search(&thread_id)
                .ok()
                .filter(|&rank| rank < colorous::SET3.len())
                .map(|rank| colorous::SET3[rank]);

            if let Some(color) = color {
                chunk[0] = color.r;
                chunk[1] = color.g;
                chunk[2] = color.b;
                chunk[3] = if filter_id == Some(thread_id) || filter_id == Some(-1) {
                    255
                } else {
                    64
                };
            } else {
                chunk[0] = 0;
                chunk[1] = 0;
                chunk[2] = 0;
                chunk[3] = 255;
            }
        }

        out
    }

    pub fn to_values(&self) -> Vec<i32> {
        self.thread_id_buffer[self.plane.range()].to_vec()
    }

    pub fn to_thread_counts(&self) -> (&[u32], &[u32]) {
        (&self.store_counts, &self.load_counts)
    }

    pub fn to_nan_overlay(&self) -> Vec<u8> {
        nan_mask(&self.values[self.plane.range()], self.plane.len)
    }

    pub fn to_inf_overlay(&self) -> Vec<u8> {
        inf_mask(&self.values[self.plane.range()], self.plane.len)
    }
}
