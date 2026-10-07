use rustc_hash::{FxHashMap, FxHashSet};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

// ── Type system ──────────────────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TypeCode {
    Int,
    Uint,
    Float,
    Handle,
    BFloat,
    Unknown(u8),
}

impl TypeCode {
    fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Int,
            1 => Self::Uint,
            2 => Self::Float,
            3 => Self::Handle,
            4 => Self::BFloat,
            other => Self::Unknown(other),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HalideType {
    pub code: TypeCode,
    pub bits: u8,
    pub lanes: u16,
}

impl HalideType {
    // Obtain the number of bytes for a single scalar element (i.e., one SIMD lane) of a packet's
    // value. For sub-byte types, this rounds up to the nearest whole byte.
    pub fn elem_bytes(self) -> usize {
        (self.bits as usize).div_ceil(8)
    }

    // Obtain the number of bytes for the entire value of a packet. This is the product of the
    // number of lanes and the size of each lane.
    pub fn value_bytes(self) -> usize {
        self.lanes as usize * self.elem_bytes()
    }
}

// ── Event codes ──────────────────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventCode {
    Load,
    Store,
    BeginRealization,
    EndRealization,
    Produce,
    EndProduce,
    Consume,
    EndConsume,
    BeginPipeline,
    EndPipeline,
    Tag,
    BeginParallelTask,
    EndParallelTask,
    BoundsRequired,
    Unknown(i32),
}

impl EventCode {
    fn from_i32(v: i32) -> Self {
        match v {
            0 => Self::Load,
            1 => Self::Store,
            2 => Self::BeginRealization,
            3 => Self::EndRealization,
            4 => Self::Produce,
            5 => Self::EndProduce,
            6 => Self::Consume,
            7 => Self::EndConsume,
            8 => Self::BeginPipeline,
            9 => Self::EndPipeline,
            10 => Self::Tag,
            11 => Self::BeginParallelTask,
            12 => Self::EndParallelTask,
            13 => Self::BoundsRequired,
            other => Self::Unknown(other),
        }
    }
}

// ── Packet view ──────────────────────────────────────────────────────────────────────────────────

#[cfg(target_endian = "big")]
compile_error!("Trace packets are read in place, which requires a little-endian host.");

/// A view of one packet, in place in the trace's data.
#[derive(Debug, Clone, Copy)]
pub struct TracePacket<'a> {
    /// This packet's own id, for the purpose of other packets' `parent_id`. Only meaningful for
    /// non-load/store events; load/store packets are leaves (nothing parents against them) and
    /// carry `value_index` in this slot instead, so `id` is meaningless for them.
    pub id: i32,
    pub event: EventCode,
    pub parent_id: i32,
    /// Which tuple element was accessed. Only meaningful for load/store events.
    pub value_index: i32,
    /// Only meaningful for load/store events.
    pub type_: HalideType,
    /// The Halide-internal thread that executed this event. For Load/Store packets (whose header
    /// reuses this slot for `type_`) it's that of the nearest enclosing `BeginParallelTask`, or -1
    /// if there is none (i.e. serial execution). Meaningless for events other than these and
    /// `BeginParallelTask`.
    pub thread_id: i32,
    /// Coordinates in dim-major / lane-minor order: [x₀..xₙ, y₀..yₙ, c₀..cₙ] where n = type_.lanes.
    pub coordinates: &'a [i32],
    pub value: &'a [u8],
    /// The null-terminated func name, followed by the null-terminated trace tag.
    names: &'a [u8],
}

impl<'a> TracePacket<'a> {
    /// A view of the packet that is exactly `words`, which must be at least a header long.
    fn new(words: &'a [u32]) -> Self {
        assert!(words.len() >= HEADER_WORDS);
        // SAFETY: `words` is long enough, and aligned enough (see the assertions on
        // halide_trace_packet_t below), to hold a header.
        let header = unsafe { &*(words.as_ptr() as *const halide_trace_packet_t) };
        let event = EventCode::from_i32(header.event as i32);
        let is_load_or_store = matches!(event, EventCode::Load | EventCode::Store);

        // `__bindgen_anon_1` is `id` for non-load/store events and `value_index` for
        // load/store events; `__bindgen_anon_2` is `type` for load/store events and
        // `thread_id` (else 0) otherwise. See halide_trace_packet_t in HalideRuntime.h.
        let (id, value_index) = if is_load_or_store {
            (0, unsafe { header.__bindgen_anon_1.value_index })
        } else {
            (unsafe { header.__bindgen_anon_1.id }, 0)
        };
        let (type_, thread_id) = if is_load_or_store {
            let inner = unsafe { header.__bindgen_anon_2.__bindgen_anon_1 };
            let type_ = HalideType {
                code: TypeCode::from_u8(inner.type_code),
                bits: inner.type_bits,
                lanes: inner.lanes,
            };
            (type_, -1)
        } else {
            let type_ = HalideType {
                code: TypeCode::from_u8(0),
                bits: 0,
                lanes: 0,
            };
            (type_, unsafe { header.__bindgen_anon_2.thread_id })
        };

        // Immediately after the header:
        //   i32  coordinates[dimensions]
        //   u8   value[type.lanes * ceil(type.bits / 8)]  (load/store events only)
        //   char func[]       (null-terminated)
        //   char trace_tag[]  (null-terminated; empty string if absent)
        let dims = (header.dimensions as usize).min(words.len() - HEADER_WORDS);
        let coords = &words[HEADER_WORDS..HEADER_WORDS + dims];
        // SAFETY: i32 and u32 have the same layout, and any bytes are valid u8s.
        let coordinates =
            unsafe { std::slice::from_raw_parts(coords.as_ptr() as *const i32, coords.len()) };
        let bytes =
            unsafe { std::slice::from_raw_parts(words.as_ptr() as *const u8, words.len() * 4) };
        let value_off = (HEADER_WORDS + dims) * 4;
        let value_len = if is_load_or_store {
            type_.value_bytes()
        } else {
            0
        };
        TracePacket {
            id,
            event,
            parent_id: header.parent_id,
            value_index,
            type_,
            thread_id,
            coordinates,
            value: bytes.get(value_off..value_off + value_len).unwrap_or(&[]),
            names: bytes.get(value_off + value_len..).unwrap_or(&[]),
        }
    }

    pub fn func(&self) -> &'a str {
        read_cstr(self.names).0
    }

    pub fn trace_tag(&self) -> &'a str {
        let (_, func_len) = read_cstr(self.names);
        read_cstr(self.names.get(func_len..).unwrap_or(&[])).0
    }

    pub fn is_load(&self) -> bool {
        self.event == EventCode::Load
    }

    pub fn is_store(&self) -> bool {
        self.event == EventCode::Store
    }

    pub fn is_load_or_store(&self) -> bool {
        self.is_load() || self.is_store()
    }

    /// Decodes lane `lane` of this packet's value into an `f64`. Returns `None` when the type isn't
    /// a decodable numeric (Handle / Unknown / an odd bit width) or when the lane runs past the
    /// value bytes. All numeric types collapse to `f64` so callers have a single comparable scalar.
    pub fn decoded_value(&self, lane: usize) -> Option<f64> {
        let elem_bytes = self.type_.elem_bytes();
        if elem_bytes == 0 {
            return None;
        }

        let off = lane * elem_bytes;
        if off + elem_bytes > self.value.len() {
            return None;
        }

        let s = &self.value[off..];
        match (self.type_.code, self.type_.bits) {
            (TypeCode::Float, 32) => Some(f32::from_le_bytes(s[..4].try_into().unwrap()) as f64),
            (TypeCode::Float, 64) => Some(f64::from_le_bytes(s[..8].try_into().unwrap())),
            (TypeCode::Int, 8) => Some(s[0] as i8 as f64),
            (TypeCode::Int, 16) => Some(i16::from_le_bytes(s[..2].try_into().unwrap()) as f64),
            (TypeCode::Int, 32) => Some(i32::from_le_bytes(s[..4].try_into().unwrap()) as f64),
            (TypeCode::Int, 64) => Some(i64::from_le_bytes(s[..8].try_into().unwrap()) as f64),
            (TypeCode::Uint, 8) => Some(s[0] as f64),
            (TypeCode::Uint, 16) => Some(u16::from_le_bytes(s[..2].try_into().unwrap()) as f64),
            (TypeCode::Uint, 32) => Some(u32::from_le_bytes(s[..4].try_into().unwrap()) as f64),
            (TypeCode::Uint, 64) => Some(u64::from_le_bytes(s[..8].try_into().unwrap()) as f64),
            // bfloat16 is the upper 16 bits of an IEEE f32; reconstruct by shifting left 16.
            (TypeCode::BFloat, 16) => {
                let bits = u16::from_le_bytes(s[..2].try_into().unwrap());
                Some(f32::from_bits((bits as u32) << 16) as f64)
            }
            _ => None,
        }
    }
}

// ── Per-Func statistics ───────────────────────────────────────────────────────

#[derive(Debug, Clone, Default)]
pub struct FuncStats {
    pub name: String,
    pub min_coords: Vec<i32>,
    pub max_coords: Vec<i32>,
    pub min_value: Option<f64>,
    pub max_value: Option<f64>,
    /// Maximum number of stores observed at any array / tensor coordinate for this Func.
    pub max_store_count: u32,
    /// Maximum number of loads observed at any array / tensor coordinate for this Func.
    pub max_load_count: u32,
    /// Maximum number of redundant stores observed at any array / tensor coordinate for this Func.
    /// A store is considered redundant when the incoming value bit-matches the previously stored
    /// value at that location AND there are no intervening loads from that location.
    pub max_redundant_store_count: u32,
    /// Maximum store-to-load distance observed across all array / tensor coordinates for this Func.
    /// Measured as the difference in global packet indices between a store and the next load from
    /// the same coordination.
    pub max_reuse_distance: u64,
}

/// The layout of every element of a Func. Logical dims 0 and 1 are the image axes, and `mins` and
/// `extents` describe dims 2 and up. Elements are stored row-major: x varies fastest, then y, then
/// dim 2, and so on.
#[derive(Debug, Clone)]
pub struct FuncGeometry {
    pub width: usize,
    pub height: usize,
    pub min_x: i32,
    pub min_y: i32,
    pub mins: Vec<i32>,
    pub extents: Vec<usize>,
}

impl FuncGeometry {
    /// The number of pixels in one 2D plane.
    pub fn pixels(&self) -> usize {
        self.width * self.height
    }

    /// The total number of elements.
    pub fn num_elements(&self) -> usize {
        self.pixels() * self.extents.iter().product::<usize>()
    }

    /// The extent of logical dim 2, or 1 if there is none.
    pub fn channels(&self) -> usize {
        self.extents.first().copied().unwrap_or(1)
    }

    /// The element index of lane `lane` of a packet with coordinates `coords`, or `None` if it
    /// falls outside the Func. Dims the packet doesn't have are taken to be at their min.
    #[inline]
    fn element_index(
        &self,
        coords: &[i32],
        lane: usize,
        n_lanes: usize,
        dims_per_lane: usize,
    ) -> Option<usize> {
        let coord = |d: usize, min: i32, extent: usize| -> Option<usize> {
            let c = if d < dims_per_lane {
                coords[d * n_lanes + lane] - min
            } else {
                0
            };
            (c >= 0 && (c as usize) < extent).then_some(c as usize)
        };
        let mut index = 0;
        for (i, (&min, &extent)) in self.mins.iter().zip(&self.extents).enumerate().rev() {
            index = index * extent + coord(i + 2, min, extent)?;
        }
        index = index * self.height + coord(1, self.min_y, self.height)?;
        Some(index * self.width + coord(0, self.min_x, self.width)?)
    }
}

/// One instance of a Func's realize, produce, or consume node: the packet index range it spans
/// and the region it covers, as interleaved (min, extent) pairs per dimension.
#[derive(Debug, Clone, Serialize)]
pub struct LiveBox {
    pub start: u32,
    pub end: u32,
    pub bounds: Vec<i32>,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct FuncLiveness {
    pub realizations: Vec<LiveBox>,
    pub productions: Vec<LiveBox>,
    pub consumptions: Vec<LiveBox>,
}

#[derive(Debug, Clone, Copy)]
enum LiveKind {
    Realization,
    Production,
    Consumption,
}

impl FuncLiveness {
    fn boxes_mut(&mut self, kind: LiveKind) -> &mut Vec<LiveBox> {
        match kind {
            LiveKind::Realization => &mut self.realizations,
            LiveKind::Production => &mut self.productions,
            LiveKind::Consumption => &mut self.consumptions,
        }
    }
}

// ── Complete trace ───────────────────────────────────────────────────────────────────────────────

// Note: We use BTreeMaps for deterministic iteration order here. We could consider switching to
// HashMaps to get O(1) lookups if we find Func lookup starts to become a bottleneck.
pub struct Trace {
    /// The packets, in place, as written by the Halide runtime.
    data: Vec<u32>,
    /// The offset in `data` of each packet, in words.
    packet_offsets: Vec<usize>,
    /// The thread that executed each packet (see `TracePacket::thread_id`).
    packet_threads: Vec<i32>,
    pub funcs: BTreeMap<String, FuncStats>,
    pub dag_edges: BTreeMap<String, BTreeSet<String>>,
    pub store_indices_by_func: BTreeMap<String, Vec<usize>>,
    pub load_indices_by_func: BTreeMap<String, Vec<usize>>,
    pub liveness_by_func: BTreeMap<String, FuncLiveness>,
    pub thread_ids_by_func: BTreeMap<String, BTreeSet<i32>>,
    pub global_max_store_count: u32,
    pub global_max_load_count: u32,
    pub global_max_redundant_store_count: u32,
    pub global_max_reuse_distance: u64,
    pub global_thread_ids: BTreeSet<i32>,
}

// ── Binary parsing helpers ───────────────────────────────────────────────────────────────────────

// Layout for `halide_trace_packet_t`'s fixed header, derived directly from HalideRuntime.h by
// bindgen (see build.rs) rather than hand-copied, so a layout change upstream is caught at compile
// time instead of silently desyncing. This only derives the struct's data layout: none of
// `halide_trace_packet_t`'s C++ accessor methods are bound, so the variable-length trailing data
// is walked by hand (see `TracePacket::new`).
mod ffi {
    #![allow(non_camel_case_types, non_upper_case_globals, dead_code)]
    include!(concat!(env!("OUT_DIR"), "/halide_trace_bindings.rs"));
}
use ffi::halide_trace_packet_t;

const HEADER_BYTES: usize = std::mem::size_of::<halide_trace_packet_t>();
const HEADER_WORDS: usize = HEADER_BYTES / 4;
const _: () = assert!(HEADER_BYTES % 4 == 0 && std::mem::align_of::<halide_trace_packet_t>() <= 4);

/// Read a null-terminated C string. Returns `(string, bytes_consumed_including_null)`.
fn read_cstr(buf: &[u8]) -> (&str, usize) {
    let null_idx = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());

    (
        std::str::from_utf8(&buf[..null_idx]).unwrap_or(""),
        null_idx + 1,
    )
}

// ── Stats helpers called during packet parsing ────────────────────────────────

// Update the min/max coordinate vectors in FuncStats based on the coordinates seen in a given
// packet. The returned coordinates represent the range [min, max).
fn update_coord_range(pkt: &TracePacket, stats: &mut FuncStats) {
    if pkt.coordinates.is_empty() {
        return;
    }

    let n_lanes = pkt.type_.lanes.max(1) as usize;
    let logical_dims = pkt.coordinates.len() / n_lanes;

    // If this is the first load/store for this Func, initialize the min/max coordinate vectors.
    // Otherwise, update the existing min/max values.
    if stats.min_coords.is_empty() {
        stats.min_coords.resize(logical_dims, 0);
        stats.max_coords.resize(logical_dims, 0);

        for d in 0..logical_dims {
            let mut mn = pkt.coordinates[d * n_lanes];
            let mut mx = mn + 1;

            for l in 1..n_lanes {
                let coord = pkt.coordinates[d * n_lanes + l];
                mn = mn.min(coord);
                mx = mx.max(coord + 1);
            }

            stats.min_coords[d] = mn;
            stats.max_coords[d] = mx;
        }
    } else {
        for d in 0..logical_dims {
            for l in 0..n_lanes {
                let coord = pkt.coordinates[d * n_lanes + l];

                stats.min_coords[d] = stats.min_coords[d].min(coord);
                stats.max_coords[d] = stats.max_coords[d].max(coord + 1);
            }
        }
    }
}

// Update the min/max value in FuncStats based on the value seen in a given packet.
/// Widens `[min_value, max_value]` to include the finite values of the lanes of `pkt` for which
/// `include_lane` is true.
fn update_value_range(
    pkt: &TracePacket,
    min_value: &mut Option<f64>,
    max_value: &mut Option<f64>,
    include_lane: impl Fn(usize) -> bool,
) {
    for i in 0..pkt.type_.lanes as usize {
        if !include_lane(i) {
            continue;
        }
        if let Some(v) = pkt.decoded_value(i) {
            match (*min_value, *max_value) {
                (None, _) => {
                    // Ensure we do not initialize min/max with NaN or Inf.
                    if v.is_nan() || v.is_infinite() {
                        continue;
                    }

                    *min_value = Some(v);
                    *max_value = Some(v);
                }
                (Some(mn), Some(mx)) => {
                    if v < mn && v.is_finite() {
                        *min_value = Some(v);
                    }

                    if v > mx && v.is_finite() {
                        *max_value = Some(v);
                    }
                }
                _ => {}
            }
        }
    }
}

/// Whether lane `lane` of `pkt` lies within `bounds`, which holds `[min, extent]` per dim with
/// each entry lane-interleaved like `pkt.coordinates` (a produce inside a vectorized loop has one
/// box per lane). A box with a different lane count than `pkt` matches if any of its lanes does.
fn lane_in_bounds(pkt: &TracePacket, lane: usize, bounds: &[i32]) -> bool {
    let n_lanes = pkt.type_.lanes.max(1) as usize;
    let dims = pkt.coordinates.len() / n_lanes;
    if dims == 0 || bounds.len() % (2 * dims) != 0 {
        return true;
    }
    let box_lanes = bounds.len() / (2 * dims);
    let in_box_lane = |bl: usize| {
        (0..dims).all(|d| {
            let c = pkt.coordinates[d * n_lanes + lane];
            let min = bounds[2 * d * box_lanes + bl];
            let extent = bounds[(2 * d + 1) * box_lanes + bl];
            c >= min && c - min < extent
        })
    };
    if box_lanes == 1 || box_lanes == n_lanes {
        in_box_lane(if box_lanes == 1 { 0 } else { lane })
    } else {
        (0..box_lanes).any(in_box_lane)
    }
}

/// Region events traced inside a vectorized loop have a min/extent pair per lane, stored
/// struct-of-vectors like load/store coordinates, and the lanes' regions may not be contiguous.
/// This returns the bounding box of the lanes' regions of a Func with `dims` dimensions.
fn bounding_box(coords: &[i32], dims: usize) -> Vec<i32> {
    if dims == 0 || coords.len() <= 2 * dims || coords.len() % (2 * dims) != 0 {
        return coords.to_vec();
    }
    let lanes = coords.len() / (2 * dims);
    (0..dims)
        .flat_map(|d| {
            let mins = &coords[2 * d * lanes..(2 * d + 1) * lanes];
            let extents = &coords[(2 * d + 1) * lanes..(2 * d + 2) * lanes];
            let min = *mins.iter().min().unwrap();
            let max = mins.iter().zip(extents).map(|(m, e)| m + e).max().unwrap();
            [min, max - min]
        })
        .collect()
}

// ── func_type_and_dim tag parsing ─────────────────────────────────────────────

fn parse_func_type_and_dim(trace_tag: &str, func: &mut FuncState) {
    // Format: "func_type_and_dim: <num_types> [code bits lanes]{num_types}
    //                             <num_dims> [min extent]{num_dims}"
    let mut tokens = trace_tag.split_whitespace();
    tokens.next(); // consume "func_type_and_dim:"

    // Skip over the type descriptions, which we don't currently use. We could consider using them
    // in the future to populate FuncStats.type if that'd be a useful addition.
    let num_types: usize = match tokens.next().and_then(|s| s.parse().ok()) {
        Some(n) => n,
        None => return,
    };

    for _ in 0..num_types * 3 {
        tokens.next();
    }

    // Parse the dimension descriptions to extract the overall min and max coordinates for the Func.
    let num_dims: usize = match tokens.next().and_then(|s| s.parse().ok()) {
        Some(n) => n,
        None => return,
    };

    // Pre-allocate the min/max coordinate vectors to avoid repeated reallocations during parsing.
    let mut min_coords = Vec::with_capacity(num_dims);
    let mut max_coords = Vec::with_capacity(num_dims);

    for _ in 0..num_dims {
        let min: i32 = match tokens.next().and_then(|s| s.parse().ok()) {
            Some(v) => v,
            None => break,
        };
        let extent: i32 = match tokens.next().and_then(|s| s.parse().ok()) {
            Some(v) => v,
            None => break,
        };

        min_coords.push(min);
        max_coords.push(min + extent);
    }

    // Assign the declared extents wholesale, overwriting anything observed so far.
    // Coordinate extents come from two sources that both write min_coords/max_coords: this tag
    // (declared realization bounds) and update_coord_range (coords observed on Load/Store).
    //
    // In practice Halide emits this tag at pipeline start, before any load/store, so the common
    // path is "tag seeds, accesses expand."
    if !min_coords.is_empty() {
        let stats = func.stats();
        stats.min_coords = min_coords;
        stats.max_coords = max_coords;
    }
}

// ── Trace loading ────────────────────────────────────────────────────────────────────────────────

extern "C" {
    fn halide_trace_is_compressed(data: *const u8, size: usize) -> bool;
    fn halide_trace_decompressed_size(data: *const u8, size: usize) -> usize;
    fn halide_trace_decompress(
        data: *const u8,
        size: usize,
        out: *mut u8,
        ctx: *mut std::ffi::c_void,
        progress: extern "C" fn(*mut std::ffi::c_void, usize),
    ) -> bool;
}

/// Reads a file into a word-aligned buffer. Returns the buffer and the file's size in bytes.
fn read_words(path: &str) -> Result<(Vec<u32>, usize), String> {
    use std::io::Read;
    let mut file = std::fs::File::open(path).map_err(|e| e.to_string())?;
    let len = file.metadata().map_err(|e| e.to_string())?.len() as usize;
    let mut words = vec![0u32; len.div_ceil(4)];
    // SAFETY: any bytes are valid u32s, and the buffer holds at least `len` bytes.
    let bytes = unsafe { std::slice::from_raw_parts_mut(words.as_mut_ptr() as *mut u8, len) };
    file.read_exact(bytes).map_err(|e| e.to_string())?;
    Ok((words, len))
}

/// Marks a Func id as absent.
const NO_FUNC: u32 = u32::MAX;

/// What later packets need to know about a non-load/store event.
#[derive(Clone, Copy)]
struct IdInfo {
    event: EventCode,
    func: u32,
    parent: i32,
    /// The thread of the nearest enclosing `BeginParallelTask` (inclusive), or -1.
    thread: i32,
    /// The Func of the nearest enclosing `Produce` (inclusive), or NO_FUNC.
    produce_func: u32,
}

/// Everything accumulated about one Func (or other named entity, such as a pipeline) while
/// parsing.
#[derive(Default)]
struct FuncState {
    name: String,
    stats: Option<FuncStats>,
    store_indices: Vec<usize>,
    load_indices: Vec<usize>,
    liveness: Option<FuncLiveness>,
    thread_ids: BTreeSet<i32>,
    last_thread_id: Option<i32>,
    /// Value range observed by loads, used only for Funcs with no stores.
    load_value_range: Option<(Option<f64>, Option<f64>)>,
}

impl FuncState {
    fn stats(&mut self) -> &mut FuncStats {
        let name = &self.name;
        self.stats.get_or_insert_with(|| FuncStats {
            name: name.clone(),
            ..Default::default()
        })
    }

    fn dims(&self) -> usize {
        self.stats.as_ref().map_or(0, |s| s.min_coords.len())
    }
}

#[derive(Default)]
struct Loader {
    packet_offsets: Vec<usize>,
    packet_threads: Vec<i32>,
    funcs: Vec<FuncState>,
    func_ids: FxHashMap<Vec<u8>, u32>,
    last_func: Option<u32>,
    ids: FxHashMap<i32, IdInfo>,
    /// The parent of the last load/store, and its thread and producing Func.
    last_parent: Option<(i32, i32, u32)>,
    /// (consumer, producer) Func pairs.
    dag_edges: FxHashSet<(u32, u32)>,
    last_dag_edge: Option<(u32, u32)>,
    global_thread_ids: BTreeSet<i32>,
    /// Begin-event id -> the LiveBox it opened, until its end event arrives.
    open_boxes: FxHashMap<i32, (LiveKind, u32, usize)>,
    /// The latest produce box of a Func, keyed by the produce's parent and by the Func's
    /// enclosing realization, for finding the produce node corresponding to a consume node.
    produce_box_by_parent: FxHashMap<(i32, u32), Vec<i32>>,
    produce_box_by_realization: FxHashMap<i32, Vec<i32>>,
    /// Produce event id -> the region it is required to compute (see `lane_in_bounds`).
    produce_bounds_by_id: FxHashMap<i32, Vec<i32>>,
    /// (parent id, func) -> the most recent BoundsRequired region declared for that func within
    /// that parent.
    bounds_required_by_parent: FxHashMap<(i32, u32), Vec<i32>>,
    /// The regions constraining the most recent store, keyed by (parent_id, func).
    last_store_bounds: Option<(i32, u32, Vec<Vec<i32>>)>,
}

impl Loader {
    /// Parses the packets in `num_words` words at `data`. Calls `wait_ready(n)` when it has
    /// parsed everything in the first `n` words available so far, which must block until more are
    /// available and return the new number available, or return `n` if no more will be.
    ///
    /// # Safety
    /// `data` must point to `num_words` words, of which those reported available by
    /// `wait_ready` are never written again.
    unsafe fn parse(
        &mut self,
        data: *const u32,
        num_words: usize,
        mut wait_ready: impl FnMut(usize) -> usize,
        on_progress: &mut dyn FnMut(String, u8),
    ) {
        let mut pos = 0;
        let mut ready = 0;
        let mut pct = 0u8;
        let mut next_report = 0;
        loop {
            let now_ready = wait_ready(ready);
            if now_ready <= ready {
                return;
            }
            ready = now_ready;
            let words = std::slice::from_raw_parts(data, ready);
            while pos + HEADER_WORDS <= ready {
                let size = words[pos] as usize;
                if size < HEADER_BYTES || size % 4 != 0 {
                    return;
                }
                let end = pos + size / 4;
                if end > ready {
                    break;
                }
                self.packet(TracePacket::new(&words[pos..end]), pos);
                pos = end;
                if pos >= next_report {
                    if pct > 0 {
                        on_progress("Loading trace...".to_string(), pct);
                    }
                    pct += 1;
                    next_report = (num_words as u64 * pct as u64 / 100) as usize;
                }
            }
        }
    }

    fn func_id(&mut self, names: &[u8]) -> u32 {
        if let Some(f) = self.last_func {
            let last = self.funcs[f as usize].name.as_bytes();
            if names.get(last.len()) == Some(&0) && names.starts_with(last) {
                return f;
            }
        }
        let name = &names[..names.iter().position(|&b| b == 0).unwrap_or(names.len())];
        let f = match self.func_ids.get(name) {
            Some(&f) => f,
            None => {
                let f = self.funcs.len() as u32;
                self.func_ids.insert(name.to_vec(), f);
                self.funcs.push(FuncState {
                    name: String::from_utf8_lossy(name).into_owned(),
                    ..Default::default()
                });
                f
            }
        };
        self.last_func = Some(f);
        f
    }

    fn packet(&mut self, mut pkt: TracePacket, offset: usize) {
        let index = self.packet_offsets.len();
        self.packet_offsets.push(offset);
        let func = self.func_id(pkt.names);
        let parent_id = pkt.parent_id;

        if pkt.is_load_or_store() {
            let (thread, produce_func) = match self.last_parent {
                Some((p, thread, produce_func)) if p == parent_id => (thread, produce_func),
                _ => {
                    let (thread, produce_func) = self
                        .ids
                        .get(&parent_id)
                        .map_or((-1, NO_FUNC), |i| (i.thread, i.produce_func));
                    self.last_parent = Some((parent_id, thread, produce_func));
                    (thread, produce_func)
                }
            };
            pkt.thread_id = thread;
            let f = &mut self.funcs[func as usize];
            if thread != -1 && f.last_thread_id != Some(thread) {
                f.last_thread_id = Some(thread);
                f.thread_ids.insert(thread);
                self.global_thread_ids.insert(thread);
            }
            // A load inside the production of another Func makes that Func a consumer.
            if pkt.is_load()
                && produce_func != NO_FUNC
                && produce_func != func
                && self.last_dag_edge != Some((func, produce_func))
            {
                self.last_dag_edge = Some((func, produce_func));
                self.dag_edges.insert((func, produce_func));
            }
        } else {
            // Load/store packets don't carry a real `id` (that slot holds `value_index` instead)
            // and are leaves that nothing ever parents against, so only other events are
            // recorded.
            let parent = self.ids.get(&parent_id).copied();
            let info = IdInfo {
                event: pkt.event,
                func,
                parent: parent_id,
                thread: if pkt.event == EventCode::BeginParallelTask {
                    pkt.thread_id
                } else {
                    parent.map_or(-1, |p| p.thread)
                },
                produce_func: if pkt.event == EventCode::Produce {
                    func
                } else {
                    parent.map_or(NO_FUNC, |p| p.produce_func)
                },
            };
            self.ids.insert(pkt.id, info);
            self.last_parent = None;
        }
        self.packet_threads.push(pkt.thread_id);

        // Coordinate extents are populated from two sources below: the func_type_and_dim tag
        // (declared bounds) and Load/Store coords (observed bounds). Both write min_coords /
        // max_coords; their interaction is order-dependent by design.
        match pkt.event {
            EventCode::Tag => {
                let tag = pkt.trace_tag();
                if tag.starts_with("func_type_and_dim:") {
                    parse_func_type_and_dim(tag, &mut self.funcs[func as usize]);
                }
            }
            EventCode::BeginRealization => {
                self.funcs[func as usize].stats();
                self.open_live_box(
                    LiveKind::Realization,
                    func,
                    pkt.id,
                    index,
                    pkt.coordinates.to_vec(),
                );
            }
            EventCode::EndRealization | EventCode::EndProduce | EventCode::EndConsume => {
                self.close_live_boxes(parent_id, index as u32);
            }
            EventCode::Load => {
                let f = &mut self.funcs[func as usize];
                f.load_indices.push(index);
                update_coord_range(&pkt, f.stats());
                let (min_value, max_value) = f.load_value_range.get_or_insert((None, None));
                update_value_range(&pkt, min_value, max_value, |_| true);
            }
            EventCode::Store => {
                // Values stored outside the enclosing produce's region, or outside any region
                // declared required by a BoundsRequired event along the parent chain, are
                // intentional overcompute (e.g. rounded-up splits) and may be garbage, so they
                // don't count towards the value range.
                let cached = self
                    .last_store_bounds
                    .as_ref()
                    .is_some_and(|(p, f, _)| *p == parent_id && *f == func);
                if !cached {
                    let mut bounds = Vec::new();
                    let mut seen_produce = false;
                    let mut ancestor = parent_id;
                    loop {
                        if let Some(b) = self.bounds_required_by_parent.get(&(ancestor, func)) {
                            bounds.push(b.clone());
                        }
                        let Some(info) = self.ids.get(&ancestor) else {
                            break;
                        };
                        if !seen_produce && info.event == EventCode::Produce && info.func == func {
                            if let Some(b) = self.produce_bounds_by_id.get(&ancestor) {
                                bounds.push(b.clone());
                            }
                            seen_produce = true;
                        }
                        if info.parent == ancestor {
                            break;
                        }
                        ancestor = info.parent;
                    }
                    self.last_store_bounds = Some((parent_id, func, bounds));
                }
                let bounds = self
                    .last_store_bounds
                    .as_ref()
                    .map_or(&[][..], |(_, _, b)| b.as_slice());
                let f = &mut self.funcs[func as usize];
                f.store_indices.push(index);
                let stats = f.stats();
                update_coord_range(&pkt, stats);
                update_value_range(&pkt, &mut stats.min_value, &mut stats.max_value, |lane| {
                    bounds.iter().all(|b| lane_in_bounds(&pkt, lane, b))
                });
            }
            EventCode::BoundsRequired => {
                self.bounds_required_by_parent
                    .insert((parent_id, func), pkt.coordinates.to_vec());
                self.last_store_bounds = None;
            }
            EventCode::Produce => {
                self.produce_bounds_by_id
                    .insert(pkt.id, pkt.coordinates.to_vec());
                let bounds = bounding_box(pkt.coordinates, self.funcs[func as usize].dims());
                self.produce_box_by_parent
                    .insert((parent_id, func), bounds.clone());
                if let Some(r) = self.enclosing_realization(parent_id, func) {
                    self.produce_box_by_realization.insert(r, bounds.clone());
                }
                self.open_live_box(LiveKind::Production, func, pkt.id, index, bounds);
            }
            EventCode::Consume => {
                let bounds = self
                    .produce_box_by_parent
                    .get(&(parent_id, func))
                    .or_else(|| {
                        self.enclosing_realization(parent_id, func)
                            .and_then(|r| self.produce_box_by_realization.get(&r))
                    })
                    .cloned()
                    .unwrap_or_else(|| {
                        bounding_box(pkt.coordinates, self.funcs[func as usize].dims())
                    });
                self.open_live_box(LiveKind::Consumption, func, pkt.id, index, bounds);
            }
            _ => {}
        }
    }

    fn open_live_box(
        &mut self,
        kind: LiveKind,
        func: u32,
        id: i32,
        start: usize,
        bounds: Vec<i32>,
    ) {
        let boxes = self.funcs[func as usize]
            .liveness
            .get_or_insert_with(Default::default)
            .boxes_mut(kind);
        boxes.push(LiveBox {
            start: start as u32,
            end: start as u32,
            bounds,
        });
        self.open_boxes.insert(id, (kind, func, boxes.len() - 1));
    }

    /// Closes the box opened by `begin_id`, along with any still-open boxes nested inside it whose
    /// own end events are missing from the trace.
    fn close_live_boxes(&mut self, begin_id: i32, end: u32) {
        let Some(closed) = self.open_boxes.remove(&begin_id) else {
            return;
        };
        let nested: Vec<i32> = self
            .open_boxes
            .keys()
            .copied()
            .filter(|&id| {
                let mut a = id;
                while let Some(info) = self.ids.get(&a) {
                    if info.parent == begin_id {
                        return true;
                    }
                    if info.parent == a {
                        break;
                    }
                    a = info.parent;
                }
                false
            })
            .collect();
        let nested: Vec<_> = nested
            .into_iter()
            .filter_map(|id| self.open_boxes.remove(&id))
            .collect();
        for (kind, func, i) in std::iter::once(closed).chain(nested) {
            self.close_live_box(kind, func, i, end);
        }
    }

    fn close_live_box(&mut self, kind: LiveKind, func: u32, i: usize, end: u32) {
        if let Some(l) = &mut self.funcs[func as usize].liveness {
            l.boxes_mut(kind)[i].end = end;
        }
    }

    /// The id of the innermost realization of `func` enclosing the event `id`.
    fn enclosing_realization(&self, mut id: i32, func: u32) -> Option<i32> {
        loop {
            let info = self.ids.get(&id)?;
            if info.event == EventCode::BeginRealization && info.func == func {
                return Some(id);
            }
            if info.parent == id {
                return None;
            }
            id = info.parent;
        }
    }

    fn finish(mut self, data: Vec<u32>, on_progress: &mut dyn FnMut(String, u8)) -> Trace {
        // Close boxes whose end event never arrived at the end of the trace.
        let last = self.packet_offsets.len().saturating_sub(1) as u32;
        for (kind, func, i) in std::mem::take(&mut self.open_boxes).into_values() {
            self.close_live_box(kind, func, i, last);
        }

        let mut dag_edges: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
        for &(consumer, producer) in &self.dag_edges {
            dag_edges
                .entry(self.funcs[consumer as usize].name.clone())
                .or_default()
                .insert(self.funcs[producer as usize].name.clone());
        }

        let mut trace = Trace {
            data,
            packet_offsets: self.packet_offsets,
            packet_threads: self.packet_threads,
            funcs: BTreeMap::new(),
            dag_edges,
            store_indices_by_func: BTreeMap::new(),
            load_indices_by_func: BTreeMap::new(),
            liveness_by_func: BTreeMap::new(),
            thread_ids_by_func: BTreeMap::new(),
            global_max_store_count: 0,
            global_max_load_count: 0,
            global_max_redundant_store_count: 0,
            global_max_reuse_distance: 0,
            global_thread_ids: self.global_thread_ids,
        };
        for mut f in self.funcs {
            if let Some(mut stats) = f.stats {
                // Loads of a produced Func can read its overcomputed garbage, so only Funcs with
                // no stores (pipeline inputs) take their value range from loads.
                if let (true, Some((min_value, max_value))) =
                    (f.store_indices.is_empty(), f.load_value_range)
                {
                    stats.min_value = min_value;
                    stats.max_value = max_value;
                }
                trace.funcs.insert(f.name.clone(), stats);
            }
            if !f.store_indices.is_empty() {
                trace
                    .store_indices_by_func
                    .insert(f.name.clone(), std::mem::take(&mut f.store_indices));
            }
            if !f.load_indices.is_empty() {
                trace
                    .load_indices_by_func
                    .insert(f.name.clone(), std::mem::take(&mut f.load_indices));
            }
            if let Some(liveness) = f.liveness.take() {
                trace.liveness_by_func.insert(f.name.clone(), liveness);
            }
            if !f.thread_ids.is_empty() {
                trace.thread_ids_by_func.insert(f.name, f.thread_ids);
            }
        }
        trace.compute_access_stats(on_progress);
        trace
    }
}

/// The maximum, over the elements of a Func, of some statistics of their accesses.
#[derive(Default)]
struct AccessStats {
    max_store_count: u32,
    max_load_count: u32,
    max_redundant_store_count: u32,
    max_reuse_distance: u64,
}

impl Trace {
    pub fn load_from_file(
        path: &str,
        mut on_progress: impl FnMut(String, u8),
    ) -> Result<Self, String> {
        let (file, len) = read_words(path)?;
        let mut loader = Loader::default();
        // Traces written by Pipeline::halidoscope are compressed; HL_TRACE_FILE traces are not.
        if !unsafe { halide_trace_is_compressed(file.as_ptr() as *const u8, len) } {
            let num_words = len / 4;
            unsafe { loader.parse(file.as_ptr(), num_words, |_| num_words, &mut on_progress) };
            return Ok(loader.finish(file, &mut on_progress));
        }

        let size = unsafe { halide_trace_decompressed_size(file.as_ptr() as *const u8, len) };
        if size == usize::MAX {
            return Err("corrupt compressed trace".to_string());
        }
        let mut data = vec![0u32; size / 4];
        let out = data.as_mut_ptr() as usize;

        // The number of bytes decompressed so far, and whether decompression has finished.
        struct Ready {
            state: std::sync::Mutex<(usize, bool)>,
            cv: std::sync::Condvar,
        }
        extern "C" fn progress(ctx: *mut std::ffi::c_void, bytes: usize) {
            let ready = unsafe { &*(ctx as *const Ready) };
            ready.state.lock().unwrap().0 = bytes;
            ready.cv.notify_all();
        }
        let ready = Ready {
            state: std::sync::Mutex::new((0, false)),
            cv: std::sync::Condvar::new(),
        };

        // Parse the decompressed prefix while the rest is decompressed.
        let ok = std::thread::scope(|s| {
            let decompressor = s.spawn(|| {
                let ok = unsafe {
                    halide_trace_decompress(
                        file.as_ptr() as *const u8,
                        len,
                        out as *mut u8,
                        &ready as *const Ready as *mut std::ffi::c_void,
                        progress,
                    )
                };
                ready.state.lock().unwrap().1 = true;
                ready.cv.notify_all();
                ok
            });
            let wait_ready = |parsed: usize| {
                let mut state = ready.state.lock().unwrap();
                while state.0 / 4 <= parsed && !state.1 {
                    state = ready.cv.wait(state).unwrap();
                }
                state.0 / 4
            };
            unsafe { loader.parse(out as *const u32, size / 4, wait_ready, &mut on_progress) };
            decompressor.join().unwrap()
        });
        drop(file);
        if !ok {
            return Err("corrupt compressed trace".to_string());
        }
        Ok(loader.finish(data, &mut on_progress))
    }

    pub fn num_packets(&self) -> usize {
        self.packet_offsets.len()
    }

    pub fn packet(&self, index: usize) -> TracePacket<'_> {
        let start = self.packet_offsets[index];
        let end = start + self.data[start] as usize / 4;
        let mut pkt = TracePacket::new(&self.data[start..end]);
        pkt.thread_id = self.packet_threads[index];
        pkt
    }

    /// Computes the max per-element store count, load count, redundant store count, and reuse
    /// distance of each Func, in parallel across Funcs.
    fn compute_access_stats(&mut self, on_progress: &mut dyn FnMut(String, u8)) {
        let mut jobs: Vec<(&str, FuncGeometry, &[usize], &[usize])> = self
            .funcs
            .iter()
            .filter_map(|(name, stats)| {
                let stores = self.func_store_indices(name).unwrap_or(&[]);
                let loads = self.func_load_indices(name).unwrap_or(&[]);
                if stores.is_empty() && loads.is_empty() {
                    return None;
                }
                Some((name.as_str(), func_geometry(stats)?, stores, loads))
            })
            .collect();
        // Start the biggest jobs first, for load balance.
        jobs.sort_by_key(|(_, _, stores, loads)| std::cmp::Reverse(stores.len() + loads.len()));

        let next_job = std::sync::atomic::AtomicUsize::new(0);
        let num_threads = std::thread::available_parallelism()
            .map_or(1, |n| n.get())
            .min(jobs.len());
        let (tx, rx) = std::sync::mpsc::channel();
        let results: Vec<(String, AccessStats)> = std::thread::scope(|s| {
            for _ in 0..num_threads {
                let tx = tx.clone();
                let (trace, jobs, next_job) = (&*self, &jobs, &next_job);
                s.spawn(move || loop {
                    let i = next_job.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some((name, geom, stores, loads)) = jobs.get(i) else {
                        break;
                    };
                    let stats = trace.access_stats(geom, stores, loads);
                    if tx.send((name.to_string(), stats)).is_err() {
                        break;
                    }
                });
            }
            drop(tx);
            let mut results = Vec::with_capacity(jobs.len());
            for result in rx {
                results.push(result);
                let pct = (results.len() * 100 / jobs.len()) as u8;
                on_progress("Analyzing trace...".to_string(), pct);
            }
            results
        });

        for (name, a) in results {
            if let Some(stats) = self.funcs.get_mut(&name) {
                stats.max_store_count = a.max_store_count;
                stats.max_load_count = a.max_load_count;
                stats.max_redundant_store_count = a.max_redundant_store_count;
                stats.max_reuse_distance = a.max_reuse_distance;
            }
            self.global_max_store_count = self.global_max_store_count.max(a.max_store_count);
            self.global_max_load_count = self.global_max_load_count.max(a.max_load_count);
            self.global_max_redundant_store_count = self
                .global_max_redundant_store_count
                .max(a.max_redundant_store_count);
            self.global_max_reuse_distance =
                self.global_max_reuse_distance.max(a.max_reuse_distance);
        }
        on_progress("Analyzing trace...".to_string(), 100);
    }

    /// The access statistics of a Func with geometry `geom` and the given store and load packet
    /// indices.
    ///
    /// For a Func with stores, all four statistics come from a single merge of its stores and
    /// loads in packet order. A store is redundant when the incoming value bit-matches the
    /// previously stored value at that location and there have been no intervening loads from that
    /// location. Reuse distance is the packet-index gap between a store and the next load from the
    /// same location.
    ///
    /// For a pipeline input (a Func with loads but no stores), the first load at each location is
    /// free (analogous to a memcpy), and subsequent loads measure distance from that first load.
    fn access_stats(&self, geom: &FuncGeometry, stores: &[usize], loads: &[usize]) -> AccessStats {
        let n = geom.num_elements();
        let mut load_counts = vec![0u32; n];
        let mut max_reuse_distances = vec![0u64; n];
        let max = |v: &[u32]| v.iter().copied().max().unwrap_or(0);

        if stores.is_empty() {
            // usize::MAX = first load hasn't occurred here yet.
            let mut first_load_at = vec![usize::MAX; n];
            for &global_idx in loads {
                for_each_lane_element(&self.packet(global_idx), geom, |_lane, i| {
                    load_counts[i] += 1;
                    if first_load_at[i] == usize::MAX {
                        first_load_at[i] = global_idx;
                    } else {
                        let dist = (global_idx - first_load_at[i]) as u64;
                        max_reuse_distances[i] = max_reuse_distances[i].max(dist);
                    }
                });
            }
            return AccessStats {
                max_load_count: max(&load_counts),
                max_reuse_distance: max_reuse_distances.iter().copied().max().unwrap_or(0),
                ..Default::default()
            };
        }

        let mut store_counts = vec![0u32; n];
        // None = no store has landed here yet; Some(bits) = last stored value as u64 bits.
        let mut last_values = vec![None::<u64>; n];
        let mut redundant_counts = vec![0u32; n];
        // usize::MAX = no store has landed here yet.
        let mut last_store_at = vec![usize::MAX; n];
        let mut si = 0;
        let mut li = 0;
        while si < stores.len() || li < loads.len() {
            let next_is_store = si < stores.len() && (li >= loads.len() || stores[si] < loads[li]);
            if next_is_store {
                let global_idx = stores[si];
                si += 1;
                let pkt = self.packet(global_idx);
                for_each_lane_element(&pkt, geom, |lane, i| {
                    store_counts[i] += 1;
                    if let Some(v) = pkt.decoded_value(lane) {
                        let v_bits = v.to_bits();
                        if last_values[i] == Some(v_bits) {
                            redundant_counts[i] += 1;
                        }
                        last_values[i] = Some(v_bits);
                    }
                    last_store_at[i] = global_idx;
                });
            } else {
                let global_idx = loads[li];
                li += 1;
                for_each_lane_element(&self.packet(global_idx), geom, |_lane, i| {
                    load_counts[i] += 1;
                    // A load resets redundancy tracking for this location: an intervening load
                    // means the next store, even if bit-identical, is not redundant.
                    last_values[i] = None;
                    if last_store_at[i] != usize::MAX {
                        let dist = (global_idx - last_store_at[i]) as u64;
                        max_reuse_distances[i] = max_reuse_distances[i].max(dist);
                    }
                });
            }
        }
        AccessStats {
            max_store_count: max(&store_counts),
            max_load_count: max(&load_counts),
            max_redundant_store_count: max(&redundant_counts),
            max_reuse_distance: max_reuse_distances.iter().copied().max().unwrap_or(0),
        }
    }

    // ── Render-path accessors ─────────────────────────────────────────────────

    /// Global packet indices of `func_name`'s store events, in ascending order. `None` if the Func
    /// emitted no stores. Use `partition_point(|&p| p <= g)` on the returned slice to turn a global
    /// timeline index `g` into the number of stores that have occurred by that point.
    pub fn func_store_indices(&self, func_name: &str) -> Option<&[usize]> {
        self.store_indices_by_func.get(func_name).map(Vec::as_slice)
    }

    pub fn func_load_indices(&self, func_name: &str) -> Option<&[usize]> {
        self.load_indices_by_func.get(func_name).map(Vec::as_slice)
    }

    /// The packets that reveal `func_name`'s values: its stores, or its loads if it has no stores
    /// (i.e. it is a pipeline input).
    pub fn func_value_indices(&self, func_name: &str) -> &[usize] {
        match self.func_store_indices(func_name) {
            Some(stores) if !stores.is_empty() => stores,
            _ => self.func_load_indices(func_name).unwrap_or(&[]),
        }
    }

    pub fn func_liveness(&self, func_name: &str) -> Option<&FuncLiveness> {
        self.liveness_by_func.get(func_name)
    }

    /// The packet index range spanned by all of `func_name`'s realizations.
    pub fn func_buffer_liveness_range(&self, func_name: &str) -> Option<(u32, u32)> {
        let r = &self.func_liveness(func_name)?.realizations;
        let start = r.iter().map(|b| b.start).min()?;
        let end = r.iter().map(|b| b.end).max()?;
        Some((start, end))
    }

    pub fn func_thread_ids(&self, func_name: &str) -> Option<&BTreeSet<i32>> {
        self.thread_ids_by_func.get(func_name)
    }

    /// The element layout of `func_name`, or `None` if it has no usable coordinate extent.
    pub fn func_geometry(&self, func_name: &str) -> Option<FuncGeometry> {
        func_geometry(self.funcs.get(func_name)?)
    }
}

// ── Shared geometry helpers ───────────────────────────────────────────────────

/// Returns `(width, height, min_x, min_y)` for a Func, or `None` if the stats
/// have no coordinate information or produce a zero-area extent.
fn func_extents(stats: &FuncStats) -> Option<(usize, usize, i32, i32)> {
    if stats.min_coords.is_empty() || stats.max_coords.is_empty() {
        return None;
    }
    let width = (stats.max_coords[0] - stats.min_coords[0]) as usize;
    let height = if stats.min_coords.len() > 1 {
        (stats.max_coords[1] - stats.min_coords[1]) as usize
    } else {
        1
    };
    if width == 0 || height == 0 {
        return None;
    }
    let min_x = stats.min_coords[0];
    let min_y = if stats.min_coords.len() > 1 {
        stats.min_coords[1]
    } else {
        0
    };
    Some((width, height, min_x, min_y))
}

/// The element layout of a Func with `stats`, or `None` if it has no usable coordinate extent.
pub fn func_geometry(stats: &FuncStats) -> Option<FuncGeometry> {
    let (width, height, min_x, min_y) = func_extents(stats)?;
    let mins = stats.min_coords.get(2..).unwrap_or(&[]).to_vec();
    let extents = mins
        .iter()
        .zip(stats.max_coords.get(2..).unwrap_or(&[]))
        .map(|(&min, &max)| (max - min).max(1) as usize)
        .collect();
    Some(FuncGeometry {
        width,
        height,
        min_x,
        min_y,
        mins,
        extents,
    })
}

/// Invokes `f(lane, index)` for each lane of `pkt` that falls within `geom`, where `index` is the
/// lane's element index (see `FuncGeometry`).
pub fn for_each_lane_element(
    pkt: &TracePacket,
    geom: &FuncGeometry,
    mut f: impl FnMut(usize, usize),
) {
    let n_lanes = pkt.type_.lanes.max(1) as usize;
    let dims_per_lane = pkt.coordinates.len() / n_lanes;
    for lane in 0..n_lanes {
        if let Some(index) = geom.element_index(pkt.coordinates, lane, n_lanes, dims_per_lane) {
            f(lane, index);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bounding_box_of_lanes() {
        // Two dims, four lanes: x mins 0, 2, 4, 6 with extent 1; y min 3 with extent 2.
        let coords = [0, 2, 4, 6, 1, 1, 1, 1, 3, 3, 3, 3, 2, 2, 2, 2];
        assert_eq!(bounding_box(&coords, 2), vec![0, 7, 3, 2]);
        assert_eq!(bounding_box(&[5, 4, 1, 2], 2), vec![5, 4, 1, 2]);
    }
}
