//! Framebuffer rendering for Halidoscope.
//!
//! A `RenderState` holds the accumulated pixel state for a single Func after applying the first
//! `applied_k` of its store events, plus everything needed to normalize and emit RGBA. It is
//! deliberately decoupled from `Trace`: the packets and the Func's store-index list are passed
//! into `seek`, so the state can live in Tauri-managed state alongside the (separately owned)
//! parsed trace without a self-referential borrow.
use ::colorous;

use crate::commands::{HeatmapMode, RenderMode};
use crate::trace::{pixel_xy, FuncGeometry, Trace, TracePacket};

pub struct RenderState {
    geom: FuncGeometry,
    min_v: f64,
    max_v: f64,
    /// Latest normalized intensity per (pixel, channel), row-major with the channel as the minor
    /// axis: `framebuffer[(y * width + x) * channels + c]`. Length is `width * height * channels`.
    /// Unwritten cells stay 0 (black).
    framebuffer: Vec<u8>,
    /// Number of this Func's store events currently reflected in `framebuffer`.
    applied_k: usize,
}

impl RenderState {
    /// Builds an empty render state for `func`, or `None` if the Func has no usable geometry (no
    /// coordinate extent / zero area).
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let stats = trace.funcs.get(func)?;
        let min_v = stats.min_value.unwrap_or(0.0);
        let max_v = stats.max_value.unwrap_or(255.0);
        let framebuffer = vec![0u8; geom.width * geom.height * geom.channels];

        Some(Self {
            geom,
            min_v,
            max_v,
            framebuffer,
            applied_k: 0,
        })
    }

    /// Brings the framebuffer to the state after the first `target_k` stores. Forward seeks apply
    /// only the delta (`applied_k..target_k`); backward seeks clear and replay from zero.
    /// `store_indices` is the Func's global packet-index list (from `Trace::func_store_indices`).
    pub fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());

        if target_k < self.applied_k {
            // Rewind: no per-pixel history is kept, so reset and replay forward.
            self.framebuffer.iter_mut().for_each(|b| *b = 0);
            self.applied_k = 0;
        }

        for &global_idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[global_idx]);
        }
        self.applied_k = target_k;
    }

    /// Writes one store packet's lanes into the framebuffer (last write wins).
    fn apply_store(&mut self, pkt: &TracePacket) {
        let n_lanes = pkt.type_.lanes.max(1) as usize;
        let dims_per_lane = pkt.coordinates.len() / n_lanes;
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for lane in 0..n_lanes {
            let Some(v) = pkt.decoded_value(lane) else {
                continue;
            };
            let (x, y) = pixel_xy(pkt, lane, n_lanes, dims_per_lane, min_x, min_y);
            if x < 0 || y < 0 || x as usize >= width || y as usize >= height {
                continue;
            }
            // Channel is logical dim 2 when present; otherwise the single plane 0.
            let c = if dims_per_lane >= 3 {
                pkt.coordinates[2 * n_lanes + lane] - min_c
            } else {
                0
            };
            if c < 0 || c as usize >= channels {
                continue;
            }
            let idx = (y as usize * width + x as usize) * channels + c as usize;
            self.framebuffer[idx] = self.normalize(v);
        }
    }

    /// Maps a decoded value to a [0, 255] intensity.
    #[inline]
    fn normalize(&self, v: f64) -> u8 {
        if self.max_v > self.min_v {
            (255.0 * (v - self.min_v) / (self.max_v - self.min_v)).clamp(0.0, 255.0) as u8
        } else {
            128
        }
    }

    /// Produces a `width * height * 4` RGBA8 buffer ready for `putImageData`. `Rgb` maps planes
    /// 0/1/2 to R/G/B (missing planes are 0); `Grayscale` replicates plane 0 across R/G/B.
    /// Alpha is always opaque.
    pub fn to_rgba(&self, mode: RenderMode) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            channels,
            ..
        } = self.geom;
        let pixels = width * height;
        let mut out = vec![0u8; pixels * 4];

        // Hoist the mode branch and the channel-count checks outside the pixel loop so each
        // inner loop is branch-free and LLVM can auto-vectorize it.
        let fb = &self.framebuffer;
        match mode {
            RenderMode::Grayscale => {
                for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                    let v = src[0];
                    chunk[0] = v;
                    chunk[1] = v;
                    chunk[2] = v;
                    chunk[3] = 255;
                }
            }
            RenderMode::Rgb => {
                if channels >= 3 {
                    for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                        chunk[0] = src[0];
                        chunk[1] = src[1];
                        chunk[2] = src[2];
                        chunk[3] = 255;
                    }
                } else if channels == 2 {
                    for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                        chunk[0] = src[0];
                        chunk[1] = src[1];
                        chunk[2] = 0;
                        chunk[3] = 255;
                    }
                } else {
                    for (chunk, src) in out.chunks_exact_mut(4).zip(fb.chunks_exact(channels)) {
                        chunk[0] = src[0];
                        chunk[1] = 0;
                        chunk[2] = 0;
                        chunk[3] = 255;
                    }
                }
            }
        }
        out
    }

    /// Number of channel planes (logical dim 2 extent, or 1).
    pub fn channels(&self) -> usize {
        self.geom.channels
    }
}

// ── Redundant computation rendering ──────────────────────────────────────────

/// Accumulated per-pixel redundant-store counts for one Func. A store to pixel (x, y, c) is
/// redundant when it writes the same bit-pattern that was last stored there. The full-trace max
/// redundant count (pre-computed at parse time) is used for normalization so the color scale is
/// stable across the entire scrub range.
pub struct RedundantState {
    geom: FuncGeometry,
    /// Last value stored per (pixel × channel), flat row-major:
    /// `last_values[(y * width + x) * channels + c]`.
    /// `None` = no store has landed here yet.
    last_values: Vec<Option<u64>>,
    /// Redundant-store count per spatial pixel, indexed by `y * width + x`.
    redundant_counts: Vec<i32>,
    applied_k: usize,
}

impl RedundantState {
    /// Builds an empty redundant state for `func`, or `None` if the Func has no usable geometry.
    pub fn new(trace: &Trace, func: &str) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let n_pixels = geom.width * geom.height;
        Some(Self {
            geom,
            last_values: vec![None; n_pixels * geom.channels],
            redundant_counts: vec![0i32; n_pixels],
            applied_k: 0,
        })
    }

    fn reset(&mut self) {
        self.last_values.iter_mut().for_each(|v| *v = None);
        self.redundant_counts.iter_mut().for_each(|c| *c = 0);
        self.applied_k = 0;
    }

    /// Seeks to the state after the first `target_k` store events. Forward seeks apply only the
    /// delta; backward seeks reset and replay from zero.
    pub fn seek(&mut self, trace: &Trace, store_indices: &[usize], target_k: usize) {
        let target_k = target_k.min(store_indices.len());
        if target_k < self.applied_k {
            self.reset();
        }
        for &global_idx in &store_indices[self.applied_k..target_k] {
            self.apply_store(&trace.packets[global_idx]);
        }
        self.applied_k = target_k;
    }

    fn apply_store(&mut self, pkt: &TracePacket) {
        let n_lanes = pkt.type_.lanes.max(1) as usize;
        let dims_per_lane = pkt.coordinates.len() / n_lanes;
        let FuncGeometry {
            width,
            height,
            channels,
            min_x,
            min_y,
            min_c,
            ..
        } = self.geom;

        for lane in 0..n_lanes {
            let Some(v) = pkt.decoded_value(lane) else {
                continue;
            };
            let (x, y) = pixel_xy(pkt, lane, n_lanes, dims_per_lane, min_x, min_y);
            if x < 0 || y < 0 || x as usize >= width || y as usize >= height {
                continue;
            }
            let c = if dims_per_lane >= 3 {
                pkt.coordinates[2 * n_lanes + lane] - min_c
            } else {
                0
            };
            if c < 0 || c as usize >= channels {
                continue;
            }
            let val_idx = (y as usize * width + x as usize) * channels + c as usize;
            let pixel_idx = y as usize * width + x as usize;
            let v_bits = v.to_bits();
            if let Some(prev_bits) = self.last_values[val_idx] {
                if prev_bits == v_bits {
                    self.redundant_counts[pixel_idx] += 1;
                }
            }
            self.last_values[val_idx] = Some(v_bits);
        }
    }

    /// Produces a `width × height × 4` RGBA8 buffer. Pixels with zero redundant stores are black;
    /// pixels with one or more are mapped through the Reds colormap, normalized against the
    /// per-Func full-trace maximum so the scale is stable while scrubbing.
    pub fn to_rgba(&self) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            max_redundant_count,
            ..
        } = self.geom;

        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });
        let scale = if max_redundant_count > 0 {
            255.0 / max_redundant_count as f64
        } else {
            0.0
        };

        let mut out = vec![0u8; width * height * 4];
        for (chunk, &count) in out.chunks_exact_mut(4).zip(self.redundant_counts.iter()) {
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
}

// ── Heatmap rendering ─────────────────────────────────────────────────────────

/// Accumulated per-pixel event counts for one Func, seekable along the global timeline. Mirrors
/// `RenderState` but tracks a count per pixel instead of the latest normalized value. Forward
/// seeks apply only the new events; backward seeks clear and replay. The full-trace max count
/// (pre-computed at parse time and stored in `FuncGeometry`) is used for normalization so the
/// color scale is stable across the entire scrub range.
pub struct HeatmapState {
    geom: FuncGeometry,
    mode: HeatmapMode,
    counts: Vec<i32>,
    applied_k: usize,
}

impl HeatmapState {
    /// Builds an empty heatmap state for `func`, or `None` if the Func has no usable geometry.
    pub fn new(trace: &Trace, func: &str, mode: HeatmapMode) -> Option<Self> {
        let geom = trace.func_geometry(func)?;
        let counts = vec![0i32; geom.width * geom.height];
        Some(Self {
            geom,
            mode,
            counts,
            applied_k: 0,
        })
    }

    /// Seeks to the state after the first `target_k` events of `new_mode`. If `new_mode` differs
    /// from the cached mode the counts are cleared first. `event_indices` must be the Func's
    /// global packet-index list for that mode.
    pub fn seek(
        &mut self,
        trace: &Trace,
        event_indices: &[usize],
        target_k: usize,
        new_mode: HeatmapMode,
    ) {
        if new_mode != self.mode {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.applied_k = 0;
            self.mode = new_mode;
        }

        let target_k = target_k.min(event_indices.len());
        if target_k < self.applied_k {
            self.counts.iter_mut().for_each(|c| *c = 0);
            self.applied_k = 0;
        }

        for &idx in &event_indices[self.applied_k..target_k] {
            self.increment_pixel(&trace.packets[idx]);
        }
        self.applied_k = target_k;
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

    /// Produces a `width × height × 4` RGBA8 buffer with the inferno colormap applied. Counts are
    /// normalized against the per-Func full-trace maximum so the scale is consistent as the
    /// playhead moves.
    pub fn to_rgba(&self) -> Vec<u8> {
        let FuncGeometry {
            width,
            height,
            max_store_count,
            max_load_count,
            ..
        } = self.geom;
        let max_count = match self.mode {
            HeatmapMode::Stores => max_store_count,
            HeatmapMode::Loads => max_load_count,
        };

        // Build a 256-entry LUT once before the pixel loop. Calling eval_continuous 256 times is
        // negligible; doing it once per pixel at 750K+ pixels is the choppy inner loop culprit.
        // 256 × 3 bytes = 768 bytes, fits entirely in L1 cache.
        let gradient = colorous::INFERNO;
        let lut: [[u8; 3]; 256] = std::array::from_fn(|i| {
            let c = gradient.eval_continuous(i as f64 / 255.0);
            [c.r, c.g, c.b]
        });
        let scale = if max_count > 0 {
            255.0 / max_count as f64
        } else {
            0.0
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
}
