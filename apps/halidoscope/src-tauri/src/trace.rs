use std::collections::{BTreeMap, BTreeSet, HashMap};

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
        (self.bits as usize + 7) / 8
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
            other => Self::Unknown(other),
        }
    }
}

// ── Parsed packet ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct TracePacket {
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
    /// The Halide-internal thread that executed a parallel task. Only meaningful for
    /// `BeginParallelTask`; zero for every other event.
    pub thread_id: i32,
    /// Coordinates in dim-major / lane-minor order: [x₀..xₙ, y₀..yₙ, c₀..cₙ] where n = type_.lanes.
    pub coordinates: Vec<i32>,
    pub value: Vec<u8>,
    pub func: String,
    pub trace_tag: String,
}

impl TracePacket {
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

#[derive(Debug, Clone)]
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

impl Default for FuncStats {
    fn default() -> Self {
        Self {
            name: String::new(),
            min_coords: vec![],
            max_coords: vec![],
            min_value: None,
            max_value: None,
            max_store_count: 0,
            max_load_count: 0,
            max_redundant_store_count: 0,
            max_reuse_distance: 0,
        }
    }
}

/// Full spatial layout of a Func: pixel dimensions plus the channel axis (logical dim 2).
#[derive(Debug, Clone, Copy)]
pub struct FuncGeometry {
    pub width: usize,
    pub height: usize,
    pub channels: usize,
    pub min_x: i32,
    pub min_y: i32,
    pub min_c: i32,
    pub max_store_count: u32,
    pub max_load_count: u32,
    pub max_redundant_store_count: u32,
    pub max_reuse_distance: u64,
}

// ── Complete trace ────────────────────────────────────────────────────────────

// Note: We use BTreeMaps for deterministic iteration order here. We could consider switching to
// HashMaps to get O(1) lookups if we find Func lookup starts to become a bottleneck.
pub struct Trace {
    pub packets: Vec<TracePacket>,
    pub funcs: BTreeMap<String, FuncStats>,
    pub dag_edges: BTreeMap<String, BTreeSet<String>>,
    pub store_indices_by_func: BTreeMap<String, Vec<usize>>,
    pub load_indices_by_func: BTreeMap<String, Vec<usize>>,
    pub buffer_liveness_range_by_func: BTreeMap<String, (u32, u32)>,
    pub produce_ranges_by_func: BTreeMap<String, Vec<(u32, u32)>>,
    pub consume_ranges_by_func: BTreeMap<String, Vec<(u32, u32)>>,
    pub thread_ids_by_func: BTreeMap<String, BTreeSet<i32>>,
}

// ── Binary parsing helpers ────────────────────────────────────────────────────

// halide_trace_packet_t fixed header: 6 × 4 bytes = 24 bytes.
//   u32  size                                         @ 0
//   i32  event                                        @ 4
//   i32  parent_id                                    @ 8
//   union { i32 id; i32 value_index; }                @ 12
//   union { type{code, bits, lanes}; i32 thread_id; } @ 16
//   i32  dimensions                                   @ 20
//
// Immediately after the header:
//   i32  coordinates[dimensions]
//   u8   value[type.lanes * ceil(type.bits / 8)]
//   char func[]       (null-terminated)
//   char trace_tag[]  (null-terminated; empty string if absent)
const HEADER_BYTES: usize = 24;

// Helper functions to read little-endian integers from a byte buffer at a given offset. try_into()
// will convert the slice to a fixed-size [u8; N] array. We inline these for performance since they
// are called in the hot path of packet parsing.
#[inline]
fn u32_le(buf: &[u8], off: usize) -> u32 {
    u32::from_le_bytes(buf[off..off + 4].try_into().unwrap())
}

#[inline]
fn i32_le(buf: &[u8], off: usize) -> i32 {
    i32::from_le_bytes(buf[off..off + 4].try_into().unwrap())
}

#[inline]
fn u16_le(buf: &[u8], off: usize) -> u16 {
    u16::from_le_bytes(buf[off..off + 2].try_into().unwrap())
}

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
fn update_value_range(pkt: &TracePacket, stats: &mut FuncStats) {
    for i in 0..pkt.type_.lanes as usize {
        if let Some(v) = pkt.decoded_value(i) {
            match (stats.min_value, stats.max_value) {
                (None, _) => {
                    // Ensure we do not initialize min/max with NaN or Inf.
                    if v.is_nan() || v.is_infinite() {
                        continue;
                    }

                    stats.min_value = Some(v);
                    stats.max_value = Some(v);
                }
                (Some(mn), Some(mx)) => {
                    if v < mn && v.is_finite() {
                        stats.min_value = Some(v);
                    }

                    if v > mx && v.is_finite() {
                        stats.max_value = Some(v);
                    }
                }
                _ => {}
            }
        }
    }
}

// ── func_type_and_dim tag parsing ─────────────────────────────────────────────

fn parse_func_type_and_dim(
    func_name: &str,
    trace_tag: &str,
    funcs: &mut BTreeMap<String, FuncStats>,
) {
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
        let entry = funcs.entry(func_name.to_owned()).or_default();
        entry.name = func_name.to_owned();
        entry.min_coords = min_coords;
        entry.max_coords = max_coords;
    }
}

// ── Trace loading ─────────────────────────────────────────────────────────────

impl Trace {
    pub fn load_from_file(path: &str) -> Result<Self, String> {
        let data = std::fs::read(path).map_err(|e| e.to_string())?;
        Self::load_from_bytes(&data)
    }

    pub fn load_from_bytes(data: &[u8]) -> Result<Self, String> {
        let total = data.len();
        let mut pos = 0;

        let mut packets: Vec<TracePacket> = Vec::new();
        let mut funcs: BTreeMap<String, FuncStats> = BTreeMap::new();
        let mut dag_edges: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
        let mut store_indices_by_func: BTreeMap<String, Vec<usize>> = BTreeMap::new();
        let mut load_indices_by_func: BTreeMap<String, Vec<usize>> = BTreeMap::new();
        let mut buffer_liveness_range_by_func: BTreeMap<String, (u32, u32)> = BTreeMap::new();
        let mut produce_ranges_by_func: BTreeMap<String, Vec<(u32, u32)>> = BTreeMap::new();
        let mut consume_ranges_by_func: BTreeMap<String, Vec<(u32, u32)>> = BTreeMap::new();
        let mut thread_ids_by_func: BTreeMap<String, BTreeSet<i32>> = BTreeMap::new();

        // id -> (event, func_name, parent_id): needed for DAG inference after all packets are
        // parsed.
        let mut id_to_info: HashMap<i32, (EventCode, String, i32)> = HashMap::new();

        // Loads we deferred for DAG inference.
        let mut pending_loads: Vec<(String, i32)> = Vec::new();

        // Parallel task starts we deferred for accumulating thread IDs by Func.
        let mut pending_parallel_tasks: Vec<(i32, i32)> = Vec::new();

        // Packet parsing loop.
        while pos + HEADER_BYTES <= total {
            let size = u32_le(data, pos) as usize;
            if size < HEADER_BYTES || pos + size > total {
                break;
            }

            // ── Fixed header fields ───────────────────────────────────────────
            let event = i32_le(data, pos + 4);
            let parent_id = i32_le(data, pos + 8);
            let dimensions = i32_le(data, pos + 20) as usize;

            let ev = EventCode::from_i32(event);
            let is_load_or_store = matches!(ev, EventCode::Load | EventCode::Store);

            // Slot @ 12 is `id` for non-load/store events and `value_index` for load/store
            // events; slot @ 16 is `type` for load/store events and `thread_id` (else 0)
            // otherwise. See halide_trace_packet_t in HalideRuntime.h.
            let (id, value_index) = if is_load_or_store {
                (0, i32_le(data, pos + 12))
            } else {
                (i32_le(data, pos + 12), 0)
            };

            let (type_, thread_id) = if is_load_or_store {
                let type_ = HalideType {
                    code: TypeCode::from_u8(data[pos + 16]),
                    bits: data[pos + 17],
                    lanes: u16_le(data, pos + 18),
                };
                (type_, 0)
            } else {
                (
                    HalideType {
                        code: TypeCode::from_u8(0),
                        bits: 0,
                        lanes: 0,
                    },
                    i32_le(data, pos + 16),
                )
            };

            let pkt_data = &data[pos..pos + size];

            // ── Variable-length trailing fields ───────────────────────────────
            let coords_off = HEADER_BYTES;
            let value_off = coords_off + dimensions * 4;
            // Only load/store packets have a value; the type/thread_id slot isn't a real
            // halide_type_t for other events, so value_bytes() must not be trusted for them.
            let value_len = if is_load_or_store {
                type_.value_bytes()
            } else {
                0
            };
            let func_off = value_off + value_len;

            let coords: Vec<i32> = (0..dimensions)
                .map(|i| i32_le(pkt_data, coords_off + i * 4))
                .collect();

            let value = pkt_data
                .get(value_off..value_off + value_len)
                .map(|s| s.to_vec())
                .unwrap_or_default();

            let (func_name, func_len) = pkt_data
                .get(func_off..)
                .map(|s| {
                    let (name, n) = read_cstr(s);
                    (name.to_owned(), n)
                })
                .unwrap_or_default();

            let tag_off = func_off + func_len;
            let trace_tag = pkt_data
                .get(tag_off..)
                .map(|s| read_cstr(s).0.to_owned())
                .unwrap_or_default();

            // ── Pipeline context propagation ─────────────────────────────────────────────────────
            // Load/store packets don't carry a real `id` (that slot holds `value_index`
            // instead) and are leaves that nothing ever parents against, so they're excluded
            // here to avoid a `value_index` colliding with and clobbering a real packet's entry.
            if !is_load_or_store {
                id_to_info.insert(id, (ev, func_name.clone(), parent_id));
            }

            // ── Build the packet ─────────────────────────────────────────────────────────────────
            let pkt = TracePacket {
                id,
                event: ev,
                parent_id,
                value_index,
                type_,
                thread_id,
                coordinates: coords,
                value,
                func: func_name.clone(),
                trace_tag: trace_tag.clone(),
            };

            // ── Update per-Func stats ─────────────────────────────────────────
            // Coordinate extents are populated from two sources below: the func_type_and_dim tag
            // (declared bounds) and Load/Store coords (observed bounds). Both write min_coords /
            // max_coords; their interaction is order-dependent by design.
            match ev {
                EventCode::Tag if trace_tag.starts_with("func_type_and_dim:") => {
                    parse_func_type_and_dim(&func_name, &trace_tag, &mut funcs);
                }
                EventCode::BeginRealization => {
                    let entry = funcs.entry(func_name.clone()).or_default();
                    entry.name = func_name.clone();

                    // Start the liveness range for this Func at the current packet index.
                    let idx = packets.len() as u32;

                    buffer_liveness_range_by_func
                        .entry(func_name.clone())
                        .and_modify(|range| range.0 = range.0.min(idx))
                        .or_insert((idx, idx));
                }
                EventCode::EndRealization => {
                    // End the liveness range for this Func at the current packet index.
                    let idx = packets.len() as u32;

                    buffer_liveness_range_by_func
                        .entry(func_name.clone())
                        .and_modify(|range| range.1 = range.1.max(idx))
                        .or_insert((idx, idx));
                }
                EventCode::Load => {
                    // When we observe a load event, add its current index (equivalent to
                    // packets.len() before the push) to our BTreeMap of load indices for this Func.
                    load_indices_by_func
                        .entry(func_name.clone())
                        .or_default()
                        .push(packets.len());

                    // Add the load event to the list of pending loads to support DAG inference.
                    pending_loads.push((func_name.clone(), parent_id));
                    let stats = funcs.entry(func_name.clone()).or_insert_with(|| FuncStats {
                        name: func_name.clone(),
                        ..Default::default()
                    });

                    // Update the min/max coordinate and value ranges for this Func based on the
                    // current load packet.
                    update_coord_range(&pkt, stats);
                    update_value_range(&pkt, stats);
                }
                EventCode::Store => {
                    // When we observe a store event, add its current index (equivalent to
                    // packets.len() before the push) to our BTreeMap of store indices for this
                    // Func.
                    store_indices_by_func
                        .entry(func_name.clone())
                        .or_default()
                        .push(packets.len());

                    // Update the min/max coordinate and value ranges for this Func based on the
                    // current store packet.
                    let stats = funcs.entry(func_name.clone()).or_insert_with(|| FuncStats {
                        name: func_name.clone(),
                        ..Default::default()
                    });
                    update_coord_range(&pkt, stats);
                    update_value_range(&pkt, stats);
                }
                EventCode::Produce => {
                    let idx = packets.len() as u32;

                    // When we observe a Produce event for a Func A, this signals that A is
                    // consuming some other Func B. Thus, we store this as A's consume range.
                    consume_ranges_by_func
                        .entry(func_name.clone())
                        .or_default()
                        .push((idx, idx));
                }
                EventCode::EndProduce => {
                    let idx = packets.len() as u32;

                    if let Some(ranges) = consume_ranges_by_func.get_mut(func_name.as_str()) {
                        if let Some(last) = ranges.last_mut() {
                            last.1 = idx;
                        }
                    }
                }
                EventCode::Consume => {
                    let idx = packets.len() as u32;

                    // When we observe a Consume event for a Func A, this signals that A is
                    // producing for (being consumed by) some other Func B. Thus, we store this as
                    // A's produce range.
                    produce_ranges_by_func
                        .entry(func_name.clone())
                        .or_default()
                        .push((idx, idx));
                }
                EventCode::EndConsume => {
                    let idx = packets.len() as u32;

                    if let Some(ranges) = produce_ranges_by_func.get_mut(func_name.as_str()) {
                        if let Some(last) = ranges.last_mut() {
                            last.1 = idx;
                        }
                    }
                }
                EventCode::BeginParallelTask => {
                    pending_parallel_tasks.push((thread_id, parent_id));
                }
                _ => {}
            }

            packets.push(pkt);
            pos += size;
        }

        // ── DAG inference ────────────────────────────────────────────────────────────────────────
        // Walk up the parent chain from each load to find the enclosing Produce event; that
        // Produce's func is a producer of the loaded func.
        for (func_name, load_parent_id) in &pending_loads {
            let loaded_func = func_name.clone();

            let mut current = *load_parent_id;
            loop {
                match id_to_info.get(&current) {
                    Some((EventCode::Produce, producing_func, _)) => {
                        if loaded_func != *producing_func {
                            dag_edges
                                .entry(loaded_func.clone())
                                .or_default()
                                .insert(producing_func.clone());
                        }
                        break;
                    }
                    Some((_, _, next_parent)) => current = *next_parent,
                    None => break,
                }
            }
        }

        // Compute the set of thread IDs used to compute each Func.
        for (thread_id, parent_id) in &pending_parallel_tasks {
            let mut current = *parent_id;
            loop {
                match id_to_info.get(&current) {
                    Some((EventCode::Produce, producing_func, _)) => {
                        thread_ids_by_func
                            .entry(producing_func.clone())
                            .or_default()
                            .insert(*thread_id);
                        break;
                    }
                    Some((_, _, next_parent)) => current = *next_parent,
                    None => break,
                }
            }
        }

        // Compute max per-pixel store/load counts for each Func using the index lists. We extract
        // extents first (shared borrow) then write back (mut borrow) to keep the two borrows of
        // `funcs` non-overlapping.
        for (func_name, indices) in &store_indices_by_func {
            let extents = funcs.get(func_name.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let mut counts = vec![0u32; w * h];
                for &idx in indices {
                    let pkt = &packets[idx];
                    for_each_lane_pixel(
                        pkt,
                        min_x,
                        min_y,
                        w,
                        h,
                        None,
                        |_lane, pixel_idx, _val_idx, _x, _y| {
                            counts[pixel_idx] += 1;
                        },
                    );
                }
                if let Some(stats) = funcs.get_mut(func_name.as_str()) {
                    stats.max_store_count = counts.iter().copied().max().unwrap_or(0);
                }
            }
        }

        for (func_name, indices) in &load_indices_by_func {
            let extents = funcs.get(func_name.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let mut counts = vec![0u32; w * h];
                for &idx in indices {
                    let pkt = &packets[idx];
                    for_each_lane_pixel(
                        pkt,
                        min_x,
                        min_y,
                        w,
                        h,
                        None,
                        |_lane, pixel_idx, _val_idx, _x, _y| {
                            counts[pixel_idx] += 1;
                        },
                    );
                }
                if let Some(stats) = funcs.get_mut(func_name.as_str()) {
                    stats.max_load_count = counts.iter().copied().max().unwrap_or(0);
                }
            }
        }

        // Compute max per-pixel redundant store counts: replay all stores for each Func, tracking
        // the last value written to each (x, y, channel). A store is redundant when the incoming
        // value bit-matches the previously stored value at that location and there have been no
        // intervening loads from that location.
        for (func_name, store_indices) in &store_indices_by_func {
            let extents = funcs.get(func_name.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let stats = funcs.get(func_name.as_str()).unwrap();
                let (channels, min_c) = if stats.min_coords.len() >= 3 {
                    (
                        (stats.max_coords[2] - stats.min_coords[2]).max(1) as usize,
                        stats.min_coords[2],
                    )
                } else {
                    (1, 0)
                };

                let load_indices = load_indices_by_func
                    .get(func_name.as_str())
                    .map(Vec::as_slice)
                    .unwrap_or(&[]);

                // None = no store has landed here yet; Some(bits) = last stored value as u64 bits.
                let mut last_values = vec![None::<u64>; w * h * channels];
                let mut redundant_counts = vec![0u32; w * h];
                let mut si = 0;
                let mut li = 0;

                while si < store_indices.len() || li < load_indices.len() {
                    let next_is_store = si < store_indices.len()
                        && (li >= load_indices.len() || store_indices[si] < load_indices[li]);

                    if next_is_store {
                        let global_idx = store_indices[si];
                        si += 1;

                        let pkt = &packets[global_idx];
                        for_each_lane_pixel(
                            pkt,
                            min_x,
                            min_y,
                            w,
                            h,
                            Some((min_c, channels)),
                            |lane, pixel_idx, val_idx, _x, _y| {
                                let Some(v) = pkt.decoded_value(lane) else {
                                    return;
                                };
                                let v_bits = v.to_bits();
                                if let Some(prev_bits) = last_values[val_idx] {
                                    if prev_bits == v_bits {
                                        redundant_counts[pixel_idx] += 1;
                                    }
                                }
                                last_values[val_idx] = Some(v_bits);
                            },
                        );
                    } else {
                        // If we observe a Load, reset the last_values slot for that location to None.
                        let global_idx = load_indices[li];
                        li += 1;
                        let pkt = &packets[global_idx];
                        for_each_lane_pixel(
                            pkt,
                            min_x,
                            min_y,
                            w,
                            h,
                            Some((min_c, channels)),
                            |_lane, _pixel_idx, val_idx, _x, _y| {
                                last_values[val_idx] = None;
                            },
                        );
                    }
                }

                if let Some(stats) = funcs.get_mut(func_name.as_str()) {
                    stats.max_redundant_store_count =
                        redundant_counts.iter().copied().max().unwrap_or(0);
                }
            }
        }

        // Compute max per-pixel reuse distance for each Func. Two separate loops handle the two
        // cases:
        //
        //   1. Intermediate Funcs (have stores): anchor = most recent store; distance measured to
        //      the next load from the same (x, y, channel). Events are two-pointer merged in
        //      global order.
        //
        //   2. Pipeline inputs (loads only, no stores): the first load at each pixel is a memcpy
        //      and is "free". Subsequent loads to the same pixel measure distance from that first
        //      load. Black = only one load ever (no reuse).
        //
        for (func_name, store_indices) in &store_indices_by_func {
            let extents = funcs.get(func_name.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let stats = funcs.get(func_name.as_str()).unwrap();
                let (channels, min_c) = if stats.min_coords.len() >= 3 {
                    (
                        (stats.max_coords[2] - stats.min_coords[2]).max(1) as usize,
                        stats.min_coords[2],
                    )
                } else {
                    (1, 0)
                };
                let load_indices = load_indices_by_func
                    .get(func_name.as_str())
                    .map(Vec::as_slice)
                    .unwrap_or(&[]);

                // usize::MAX = no store has landed at this (x, y, channel) yet.
                let mut last_store_at = vec![usize::MAX; w * h * channels];
                let mut max_reuse_distances = vec![0u64; w * h];
                let mut si = 0;
                let mut li = 0;

                while si < store_indices.len() || li < load_indices.len() {
                    let next_is_store = si < store_indices.len()
                        && (li >= load_indices.len() || store_indices[si] < load_indices[li]);

                    if next_is_store {
                        let global_idx = store_indices[si];
                        si += 1;
                        let pkt = &packets[global_idx];
                        for_each_lane_pixel(
                            pkt,
                            min_x,
                            min_y,
                            w,
                            h,
                            Some((min_c, channels)),
                            |_lane, _pixel_idx, val_idx, _x, _y| {
                                last_store_at[val_idx] = global_idx;
                            },
                        );
                    } else {
                        let global_idx = load_indices[li];
                        li += 1;
                        let pkt = &packets[global_idx];
                        for_each_lane_pixel(
                            pkt,
                            min_x,
                            min_y,
                            w,
                            h,
                            Some((min_c, channels)),
                            |_lane, pixel_idx, val_idx, _x, _y| {
                                if last_store_at[val_idx] != usize::MAX {
                                    let dist = (global_idx - last_store_at[val_idx]) as u64;
                                    if dist > max_reuse_distances[pixel_idx] {
                                        max_reuse_distances[pixel_idx] = dist;
                                    }
                                }
                            },
                        );
                    }
                }

                if let Some(stats) = funcs.get_mut(func_name.as_str()) {
                    stats.max_reuse_distance =
                        max_reuse_distances.iter().copied().max().unwrap_or(0);
                }
            }
        }

        // Pipeline inputs: Funcs with loads but no stores. The first load at each (x, y, channel)
        // is free (analogous to a memcpy). Subsequent loads measure distance from that first load.
        for (func_name, load_indices) in &load_indices_by_func {
            if store_indices_by_func.contains_key(func_name.as_str()) {
                continue; // handled by the store-anchor loop above
            }
            let extents = funcs.get(func_name.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let stats = funcs.get(func_name.as_str()).unwrap();
                let (channels, min_c) = if stats.min_coords.len() >= 3 {
                    (
                        (stats.max_coords[2] - stats.min_coords[2]).max(1) as usize,
                        stats.min_coords[2],
                    )
                } else {
                    (1, 0)
                };

                // usize::MAX = first load hasn't occurred at this (x, y, channel) yet.
                let mut first_load_at = vec![usize::MAX; w * h * channels];
                let mut max_reuse_distances = vec![0u64; w * h];

                for &global_idx in load_indices {
                    let pkt = &packets[global_idx];
                    for_each_lane_pixel(
                        pkt,
                        min_x,
                        min_y,
                        w,
                        h,
                        Some((min_c, channels)),
                        |_lane, pixel_idx, val_idx, _x, _y| {
                            if first_load_at[val_idx] == usize::MAX {
                                first_load_at[val_idx] = global_idx;
                            } else {
                                let dist = (global_idx - first_load_at[val_idx]) as u64;
                                if dist > max_reuse_distances[pixel_idx] {
                                    max_reuse_distances[pixel_idx] = dist;
                                }
                            }
                        },
                    );
                }

                if let Some(stats) = funcs.get_mut(func_name.as_str()) {
                    stats.max_reuse_distance =
                        max_reuse_distances.iter().copied().max().unwrap_or(0);
                }
            }
        }

        Ok(Self {
            packets,
            funcs,
            dag_edges,
            store_indices_by_func,
            load_indices_by_func,
            buffer_liveness_range_by_func,
            produce_ranges_by_func,
            consume_ranges_by_func,
            thread_ids_by_func,
        })
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

    pub fn func_buffer_liveness_range(&self, func_name: &str) -> Option<&(u32, u32)> {
        self.buffer_liveness_range_by_func.get(func_name)
    }

    pub fn func_produce_ranges(&self, func_name: &str) -> Option<&[(u32, u32)]> {
        self.produce_ranges_by_func
            .get(func_name)
            .map(Vec::as_slice)
    }

    pub fn func_consume_ranges(&self, func_name: &str) -> Option<&[(u32, u32)]> {
        self.consume_ranges_by_func
            .get(func_name)
            .map(Vec::as_slice)
    }

    pub fn func_thread_ids(&self, func_name: &str) -> Option<&BTreeSet<i32>> {
        self.thread_ids_by_func.get(func_name)
    }

    /// Spatial layout for `func_name`, or `None` if it has no usable coordinate extent. Reuses
    /// `func_extents` for pixel dims so the renderer and the metadata layer agree, and adds the
    /// channel axis (logical dim 2).
    pub fn func_geometry(&self, func_name: &str) -> Option<FuncGeometry> {
        let stats = self.funcs.get(func_name)?;
        let (width, height, min_x, min_y) = func_extents(stats)?;
        let (channels, min_c) = if stats.min_coords.len() >= 3 {
            (
                (stats.max_coords[2] - stats.min_coords[2]).max(0) as usize,
                stats.min_coords[2],
            )
        } else {
            (1, 0)
        };
        Some(FuncGeometry {
            width,
            height,
            channels: channels.max(1),
            min_x,
            min_y,
            min_c,
            max_store_count: stats.max_store_count,
            max_load_count: stats.max_load_count,
            max_redundant_store_count: stats.max_redundant_store_count,
            max_reuse_distance: stats.max_reuse_distance,
        })
    }
}

// ── Shared geometry helpers ───────────────────────────────────────────────────

/// Returns `(width, height, min_x, min_y)` for a Func, or `None` if the stats
/// have no coordinate information or produce a zero-area extent.
pub fn func_extents(stats: &FuncStats) -> Option<(usize, usize, i32, i32)> {
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

/// Returns the `(x, y)` canvas pixel for lane `l` of `pkt`, relative to the
/// Func's origin. Caller must bounds-check before indexing the canvas.
#[inline]
pub(crate) fn pixel_xy(
    pkt: &TracePacket,
    lane: usize,
    n_lanes: usize,
    dims_per_lane: usize,
    min_x: i32,
    min_y: i32,
) -> (i32, i32) {
    let x = pkt.coordinates[lane] - min_x;
    let y = if dims_per_lane >= 2 {
        pkt.coordinates[n_lanes + lane] - min_y
    } else {
        -min_y
    };
    (x, y)
}

/// Iterates over each lane of `pkt` that falls within the Func's `w x h` extents, invoking
/// `f(lane, pixel_idx, val_idx)`. `pixel_idx` is the flattened `y * w + x` location.
///
/// When `channel` is `Some((min_c, channels))`, lanes are additionally filtered to those whose
/// channel coordinate falls within `0..channels`, and `val_idx` is the flattened
/// `pixel_idx * channels + c` location; otherwise `val_idx` is just `pixel_idx`.
pub fn for_each_lane_pixel(
    pkt: &TracePacket,
    min_x: i32,
    min_y: i32,
    w: usize,
    h: usize,
    channel: Option<(i32, usize)>,
    mut f: impl FnMut(usize, usize, usize, i32, i32),
) {
    let n_lanes = pkt.type_.lanes.max(1) as usize;
    let dims_per_lane = pkt.coordinates.len() / n_lanes;
    for lane in 0..n_lanes {
        let (x, y) = pixel_xy(pkt, lane, n_lanes, dims_per_lane, min_x, min_y);
        if x < 0 || y < 0 || x as usize >= w || y as usize >= h {
            continue;
        }

        let pixel_idx = y as usize * w + x as usize;
        let val_idx = if let Some((min_c, channels)) = channel {
            let c = if dims_per_lane >= 3 {
                pkt.coordinates[2 * n_lanes + lane] - min_c
            } else {
                0
            };

            if c < 0 || c as usize >= channels {
                continue;
            }

            pixel_idx * channels + c as usize
        } else {
            pixel_idx
        };

        f(lane, pixel_idx, val_idx, x, y);
    }
}
