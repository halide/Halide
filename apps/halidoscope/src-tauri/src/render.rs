use ::colorous;
use serde::Deserialize;

use crate::trace::{for_each_lane_pixel, pixel_xy, FuncGeometry, Trace, TracePacket};

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

    fn register(trace: &Trace, func: &str) -> Option<Self>;
    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize);
    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8>;
    fn to_values(&self) -> Vec<Self::Value>;
}

// ── Grayscale rendering ──────────────────────────────────────────────────────────────────────────

pub struct GrayscaleState {
    geom: FuncGeometry,
    min_v: f64,
    max_v: f64,
    framebuffer: Vec<u8>,
    values: Vec<f64>,
    applied_k: usize,
}

impl GrayscaleState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let stats = trace.funcs.get(func)?;
        let min_v = stats.min_value.unwrap_or(0.0);
        let max_v = stats.max_value.unwrap_or(255.0);
        let framebuffer = vec![0u8; geom.width * geom.height * geom.channels];
        let values = vec![0f64; geom.width * geom.height * geom.channels];

        Some(Self {
            geom,
            min_v,
            max_v,
            framebuffer,
            values,
            applied_k: 0,
        })
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |lane, _pixel_idx, val_idx, _x, _y| {
                let Some(v) = pkt.decoded_value(lane) else {
                    return;
                };

                self.framebuffer[val_idx] = self.normalize(v);
                self.values[val_idx] = v;
            },
        );
    }

    #[inline]
    fn normalize(&self, v: f64) -> u8 {
        (255.0 * (v - self.min_v) / (self.max_v - self.min_v)).clamp(0.0, 255.0) as u8
    }
}

impl Renderer for GrayscaleState {
    type Value = f64;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.framebuffer.iter_mut().for_each(|b| *b = 0);
            self.applied_k = 0;
        }
        for &global_idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[global_idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            channels,
            ..
        } = self.geom;
        let mut out = vec![0u8; width * height * 4];
        let fb = &self.framebuffer;
        if channels >= 3 {
            for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                // Use grayscale weights from scikit-image:
                // https://scikit-image.org/docs/stable/auto_examples/color_exposure/plot_rgb_to_gray.html
                let gray = src[0] as f64 * 0.2125 + src[1] as f64 * 0.7154 + src[2] as f64 * 0.0721;
                chunk[0] = gray as u8;
                chunk[1] = gray as u8;
                chunk[2] = gray as u8;
                chunk[3] = 255;
            }
        } else {
            for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                chunk[0] = src[0];
                chunk[1] = src[0];
                chunk[2] = src[0];
                chunk[3] = 255;
            }
        }
        out
    }

    fn to_values(&self) -> Vec<f64> {
        self.values.clone()
    }
}

// ── RGB rendering ────────────────────────────────────────────────────────────────────────────────

pub struct RgbState {
    geom: FuncGeometry,
    min_v: f64,
    max_v: f64,
    framebuffer: Vec<u8>,
    values: Vec<f64>,
    applied_k: usize,
}

impl RgbState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let stats = trace.funcs.get(func)?;
        let min_v = stats.min_value.unwrap_or(0.0);
        let max_v = stats.max_value.unwrap_or(255.0);
        let framebuffer = vec![0u8; geom.width * geom.height * geom.channels];
        let values = vec![0f64; geom.width * geom.height * geom.channels];

        Some(Self {
            geom,
            min_v,
            max_v,
            framebuffer,
            values,
            applied_k: 0,
        })
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |lane, _pixel_idx, val_idx, _x, _y| {
                let Some(v) = pkt.decoded_value(lane) else {
                    return;
                };

                self.framebuffer[val_idx] = self.normalize(v);
                self.values[val_idx] = v;
            },
        );
    }

    #[inline]
    fn normalize(&self, v: f64) -> u8 {
        (255.0 * (v - self.min_v) / (self.max_v - self.min_v)).clamp(0.0, 255.0) as u8
    }
}

impl Renderer for RgbState {
    type Value = f64;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.framebuffer.iter_mut().for_each(|b| *b = 0);
            self.applied_k = 0;
        }
        for &global_idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[global_idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            channels,
            ..
        } = self.geom;
        let mut out = vec![0u8; width * height * 4];
        let fb = &self.framebuffer;
        if channels >= 3 {
            for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                chunk[0] = src[0];
                chunk[1] = src[1];
                chunk[2] = src[2];
                chunk[3] = 255;
            }
        } else {
            for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                chunk[0] = src[0];
                chunk[1] = src[0];
                chunk[2] = src[0];
                chunk[3] = 255;
            }
        }
        out
    }

    fn to_values(&self) -> Vec<f64> {
        self.values.clone()
    }
}

// ── Store frequency rendering ────────────────────────────────────────────────────────────────────

pub struct StoreFrequencyState {
    geom: FuncGeometry,
    counts: Vec<u32>,
    local_max_store_count: u32,
    global_max_store_count: u32,
    applied_k: usize,
}

impl StoreFrequencyState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let counts = vec![0u32; geom.width * geom.height];

        let local_max_store_count = trace.funcs.get(func).map_or(0, |s| s.max_store_count);
        let global_max_store_count = trace
            .funcs
            .values()
            .map(|s| s.max_store_count)
            .max()
            .unwrap_or(0);

        Some(Self {
            geom,
            counts,
            local_max_store_count,
            global_max_store_count,
            applied_k: 0,
        })
    }

    fn increment_pixel(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            min_x,
            min_y,
            ..
        } = self.geom;
        let n_lanes = pkt.type_.lanes.max(1) as usize;
        let dims_per_lane = pkt.coordinates.len() / n_lanes;
        for l in 0..n_lanes {
            let (x, y) = pixel_xy(pkt, l, n_lanes, dims_per_lane, min_x, min_y);

            if x >= 0 && y >= 0 && (x as usize) < width && (y as usize) < height {
                self.counts[y as usize * width + x as usize] += 1;
            }
        }
    }

    pub fn to_histogram(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_store_count,
            NormalizationMode::PerFunc => self.local_max_store_count,
        };
        let mut hist = vec![0u32; max as usize + 1];

        for &c in &self.counts {
            hist[c.clamp(0, max) as usize] += 1;
        }

        hist
    }
}

impl Renderer for StoreFrequencyState {
    type Value = u32;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.applied_k = 0;
        }
        for &idx in &store_indices[self.applied_k..target_k] {
            self.increment_pixel(&trace.packets[idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry { width, height, .. } = self.geom;
        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });

        let scale = match (normalization_mode, self.global_max_store_count) {
            (NormalizationMode::AcrossFuncs, 0) => 0.0,
            (NormalizationMode::AcrossFuncs, global_max) => 255.0 / global_max as f64,
            (NormalizationMode::PerFunc, _) => {
                let local_max = *&self.local_max_store_count;
                if local_max > 0 {
                    255.0 / local_max as f64
                } else {
                    0.0
                }
            }
        };

        let mut out = vec![0u8; width * height * 4];
        for (chunk, &count) in out.chunks_exact_mut(4).zip(self.counts.iter()) {
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
        self.counts.clone()
    }
}

// ── Load frequency rendering ─────────────────────────────────────────────────────────────────────

pub struct LoadFrequencyState {
    geom: FuncGeometry,
    counts: Vec<u32>,
    local_max_load_count: u32,
    global_max_load_count: u32,
    applied_k: usize,
}

impl LoadFrequencyState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let counts = vec![0u32; geom.width * geom.height];

        let local_max_load_count = trace.funcs.get(func).map_or(0, |s| s.max_load_count);
        let global_max_load_count = trace
            .funcs
            .values()
            .map(|s| s.max_load_count)
            .max()
            .unwrap_or(0);

        Some(Self {
            geom,
            counts,
            local_max_load_count,
            global_max_load_count,
            applied_k: 0,
        })
    }

    fn increment_pixel(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            min_x,
            min_y,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            None,
            |_lane, pixel_idx, _val_idx, _x, _y| {
                self.counts[pixel_idx] += 1;
            },
        );
    }

    pub fn to_histogram(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_load_count,
            NormalizationMode::PerFunc => self.local_max_load_count,
        };
        let mut hist = vec![0u32; max as usize + 1];

        for &c in &self.counts {
            hist[c.clamp(0, max) as usize] += 1;
        }

        hist
    }
}

impl Renderer for LoadFrequencyState {
    type Value = u32;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, load_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(load_indices.len());
        if target_k < self.applied_k {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.applied_k = 0;
        }
        for &idx in &load_indices[self.applied_k..target_k] {
            self.increment_pixel(&trace.packets[idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry { width, height, .. } = self.geom;
        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });

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

        let mut out = vec![0u8; width * height * 4];
        for (chunk, &count) in out.chunks_exact_mut(4).zip(self.counts.iter()) {
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
        self.counts.clone()
    }
}

// ── Redundant store rendering ────────────────────────────────────────────────────────────────────

pub struct RedundantState {
    geom: FuncGeometry,
    last_values: Vec<Option<u64>>,
    redundant_store_counts: Vec<u32>,
    local_max_redundant_store_count: u32,
    global_max_redundant_store_count: u32,
    applied_k: usize,
}

impl RedundantState {
    /// Builds an empty redundant state for `func`, or `None` if the Func has no usable geometry.
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let n_pixels = geom.width * geom.height;

        let local_max_redundant_store_count =
            trace.funcs.get(func).map(|s| s.max_redundant_store_count)?;
        let global_max_redundant_store_count = trace
            .funcs
            .values()
            .map(|s| s.max_redundant_store_count)
            .max()?;

        Some(Self {
            geom,
            last_values: vec![None; n_pixels * geom.channels],
            redundant_store_counts: vec![0u32; n_pixels],
            local_max_redundant_store_count,
            global_max_redundant_store_count,
            applied_k: 0,
        })
    }

    fn reset(&mut self) {
        self.last_values.iter_mut().for_each(|v| *v = None);
        self.redundant_store_counts.iter_mut().for_each(|c| *c = 0);
        self.applied_k = 0;
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |lane, pixel_idx: usize, val_idx: usize, _x, _y| {
                let Some(v) = pkt.decoded_value(lane) else {
                    return;
                };

                let v_bits = v.to_bits();
                if let Some(prev_bits) = self.last_values[val_idx] {
                    if prev_bits == v_bits {
                        self.redundant_store_counts[pixel_idx] += 1;
                    }
                }
                self.last_values[val_idx] = Some(v_bits);
            },
        );
    }

    pub fn to_histogram(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_redundant_store_count,
            NormalizationMode::PerFunc => self.local_max_redundant_store_count,
        };
        let mut hist = vec![0u32; max as usize + 1];
        for &c in &self.redundant_store_counts {
            hist[c.clamp(0, max) as usize] += 1;
        }
        hist
    }
}

impl Renderer for RedundantState {
    type Value = u32;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.reset();
        }
        for &global_idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[global_idx]);
        }
        self.applied_k = target_k;
    }

    fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry { width, height, .. } = self.geom;

        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });

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

        let mut out = vec![0u8; width * height * 4];
        for (chunk, &count) in out
            .chunks_exact_mut(4)
            .zip(self.redundant_store_counts.iter())
        {
            if count > 0 {
                let ti = (count as f64 * scale) as usize;
                let [r, g, b] = lut[ti.min(255)];
                chunk[0] = r;
                chunk[1] = g;
                chunk[2] = b;
            }
            chunk[3] = 255;
        }
        out
    }

    fn to_values(&self) -> Vec<u32> {
        self.redundant_store_counts.clone()
    }
}

// ── Reuse distance rendering ─────────────────────────────────────────────────────────────────────

/// Per-pixel maximum reuse distance for one Func, seekable along the global timeline.
///
/// For intermediate Funcs (those with stores) the anchor is the most recent store per
/// `(x, y, channel)`; reuse distance is measured to the next load from the same location.
///
/// For pipeline inputs (loads only, no stores) the anchor is the *first* load per location —
/// treating that load as a free "memcpy" — and subsequent loads measure distance from it.
///
/// Both the store and load index lists are merged in global order during seeking.
/// Backward seeks reset and replay from zero.
pub struct ReuseDistanceState {
    geom: FuncGeometry,
    is_input: bool,
    /// Per `(x, y, channel)` anchor, flat row-major: `anchor_at[(y * width + x) * channels + c]`.
    /// For intermediate Funcs: global index of the most recent store (`usize::MAX` = none yet).
    /// For inputs: global index of the first load (`usize::MAX` = none yet).
    anchor_at: Vec<usize>,
    /// Maximum observed reuse distance per spatial pixel, indexed by `y * width + x`.
    max_reuse_distance: Vec<u64>,
    local_max_reuse_distance: u64,
    global_max_reuse_distance: u64,
    applied_store_k: usize,
    applied_load_k: usize,
}

impl ReuseDistanceState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let n_cells = geom.width * geom.height * geom.channels;
        let is_input = trace
            .func_store_indices(func)
            .map_or(true, |s| s.is_empty());

        let local_max_reuse_distance = trace.funcs.get(func).map(|s| s.max_reuse_distance)?;
        let global_max_reuse_distance = trace.funcs.values().map(|s| s.max_reuse_distance).max()?;

        Some(Self {
            geom,
            is_input,
            anchor_at: vec![usize::MAX; n_cells],
            max_reuse_distance: vec![0u64; geom.width * geom.height],
            local_max_reuse_distance,
            global_max_reuse_distance,
            applied_store_k: 0,
            applied_load_k: 0,
        })
    }

    fn reset(&mut self) {
        self.anchor_at.iter_mut().for_each(|v| *v = usize::MAX);
        self.max_reuse_distance.iter_mut().for_each(|d| *d = 0);
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

        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |_lane, _pixel_idx, val_idx, _x, _y| {
                self.anchor_at[val_idx] = global_idx;
            },
        );
    }

    fn apply_load(&mut self, pkt: &TracePacket, global_idx: usize) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |_lane, pixel_idx, val_idx, _x, _y| {
                if self.is_input {
                    // First load is the free memcpy; establish the anchor and record no distance.
                    // Subsequent loads to the same location measure from that first load.
                    if self.anchor_at[val_idx] == usize::MAX {
                        self.anchor_at[val_idx] = global_idx;
                    } else {
                        let dist = (global_idx - self.anchor_at[val_idx]) as u64;
                        self.max_reuse_distance[pixel_idx] =
                            self.max_reuse_distance[pixel_idx].max(dist);
                    }
                } else if self.anchor_at[val_idx] != usize::MAX {
                    let dist = (global_idx - self.anchor_at[val_idx]) as u64;
                    self.max_reuse_distance[pixel_idx] =
                        self.max_reuse_distance[pixel_idx].max(dist);
                }
            },
        );
    }

    pub fn to_rgba(&self, normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry { width, height, .. } = self.geom;

        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });

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

        let mut out = vec![0u8; width * height * 4];
        for (chunk, &dist) in out.chunks_exact_mut(4).zip(self.max_reuse_distance.iter()) {
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

    pub fn to_histogram(&self, normalization_mode: NormalizationMode) -> Vec<u32> {
        let max = match normalization_mode {
            NormalizationMode::AcrossFuncs => self.global_max_reuse_distance,
            NormalizationMode::PerFunc => self.local_max_reuse_distance,
        };

        let mut hist = vec![0u32; 64];
        if max > 0 {
            for &dist in &self.max_reuse_distance {
                if dist > 0 {
                    let bucket = ((dist as f64 / max as f64) * 63.0) as usize;
                    hist[bucket.min(63)] += 1;
                }
            }
        }

        hist
    }

    /// Returns the per-pixel maximum reuse distance at the current seek position, in row-major
    /// `(y * width + x)` order.
    pub fn to_values(&self) -> Vec<u64> {
        self.max_reuse_distance.clone()
    }
}

// ── NaN Rendering ────────────────────────────────────────────────────────────────────────────────

pub struct NaNState {
    geom: FuncGeometry,
    values: Vec<f64>,
    nanbuffer: Vec<u8>,
    applied_k: usize,
}

impl NaNState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let nanbuffer = vec![0u8; geom.width * geom.height * geom.channels];
        let values = vec![0f64; geom.width * geom.height * geom.channels];

        Some(Self {
            geom,
            values,
            nanbuffer,
            applied_k: 0,
        })
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |lane, _pixel_idx: usize, val_idx: usize, _x, _y| {
                let Some(v) = pkt.decoded_value(lane) else {
                    return;
                };

                self.nanbuffer[val_idx] = if v.is_nan() { 1 } else { 0 };
                self.values[val_idx] = v;
            },
        );
    }
}

impl Renderer for NaNState {
    type Value = f64;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.values.iter_mut().for_each(|v| *v = 0.0);
            self.nanbuffer.iter_mut().for_each(|b| *b = 0);
            self.applied_k = 0;
        }

        for &idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[idx]);
        }

        self.applied_k = target_k;
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            channels,
            ..
        } = self.geom;
        let mut out = vec![0u8; width * height * 4];
        let nb = &self.nanbuffer;
        if channels >= 3 {
            for (chunk, src) in out.chunks_exact_mut(4).zip(nb.chunks_exact(channels)) {
                // If any channel is NaN, mark the pixel as cyan, otherwise transparent.
                if src[0] == 1 || src[1] == 1 || src[2] == 1 {
                    chunk[0] = 0;
                    chunk[1] = 255;
                    chunk[2] = 255;
                    chunk[3] = 255;
                } else {
                    chunk[0] = 0;
                    chunk[1] = 0;
                    chunk[2] = 0;
                    chunk[3] = 0;
                }
            }
        } else {
            for (chunk, src) in out.chunks_exact_mut(4).zip(nb.chunks_exact(channels)) {
                // If the channel is NaN, mark the pixel as cyan, otherwise transparent.
                if src[0] == 1 {
                    chunk[0] = 0;
                    chunk[1] = 255;
                    chunk[2] = 255;
                    chunk[3] = 255;
                } else {
                    chunk[0] = 0;
                    chunk[1] = 0;
                    chunk[2] = 0;
                    chunk[3] = 0;
                }
            }
        }
        out
    }

    fn to_values(&self) -> Vec<Self::Value> {
        self.values.clone()
    }
}

// ── Inf Rendering ────────────────────────────────────────────────────────────────────────────────

pub struct InfState {
    geom: FuncGeometry,
    values: Vec<f64>,
    infbuffer: Vec<u8>,
    applied_k: usize,
}

impl InfState {
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let infbuffer = vec![0u8; geom.width * geom.height * geom.channels];
        let values = vec![0f64; geom.width * geom.height * geom.channels];

        Some(Self {
            geom,
            values,
            infbuffer,
            applied_k: 0,
        })
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for_each_lane_pixel(
            pkt,
            min_x,
            min_y,
            width,
            height,
            Some((min_c, channels)),
            |lane, _pixel_idx: usize, val_idx: usize, _x, _y| {
                let Some(v) = pkt.decoded_value(lane) else {
                    return;
                };

                self.infbuffer[val_idx] = if v.is_infinite() { 1 } else { 0 };
                self.values[val_idx] = v;
            },
        );
    }
}

impl Renderer for InfState {
    type Value = f64;

    fn register(trace: &Trace, func: &str) -> Option<Self> {
        Self::new(trace, func)
    }

    fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.values.iter_mut().for_each(|v| *v = 0.0);
            self.infbuffer.iter_mut().for_each(|b| *b = 0);
            self.applied_k = 0;
        }

        for &idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[idx]);
        }

        self.applied_k = target_k;
    }

    fn to_rgba(&self, _normalization_mode: NormalizationMode) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            channels,
            ..
        } = self.geom;
        let mut out = vec![0u8; width * height * 4];
        let ib = &self.infbuffer;
        if channels >= 3 {
            for (chunk, src) in out.chunks_exact_mut(4).zip(ib.chunks_exact(channels)) {
                // If any channel is inf, mark the pixel as magenta, otherwise transparent.
                if src[0] == 1 || src[1] == 1 || src[2] == 1 {
                    chunk[0] = 255;
                    chunk[1] = 255;
                    chunk[2] = 0;
                    chunk[3] = 255;
                } else {
                    chunk[0] = 0;
                    chunk[1] = 0;
                    chunk[2] = 0;
                    chunk[3] = 0;
                }
            }
        } else {
            for (chunk, src) in out.chunks_exact_mut(4).zip(ib.chunks_exact(channels)) {
                // If the channel is inf, mark the pixel as magenta, otherwise transparent.
                if src[0] == 1 {
                    chunk[0] = 255;
                    chunk[1] = 255;
                    chunk[2] = 0;
                    chunk[3] = 255;
                } else {
                    chunk[0] = 0;
                    chunk[1] = 0;
                    chunk[2] = 0;
                    chunk[3] = 0;
                }
            }
        }
        out
    }

    fn to_values(&self) -> Vec<Self::Value> {
        self.values.clone()
    }
}
