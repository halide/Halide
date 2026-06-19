use std::collections::{BTreeMap, BTreeSet, HashMap};

// ── Type system ───────────────────────────────────────────────────────────────

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
    // Obtain the number of bytes for a single scalar element (i.e., one lane)
    // of a packet's value. For sub-byte types, this rounds up to the nearest
    // whole byte.
    pub fn elem_bytes(self) -> usize {
        (self.bits as usize + 7) / 8
    }

    // Obtain the number of bytes for the entire value of a packet.
    // This is the product of the number of lanes and the size of each lane.
    pub fn value_bytes(self) -> usize {
        self.lanes as usize * self.elem_bytes()
    }
}

// ── Event codes ───────────────────────────────────────────────────────────────

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
            other => Self::Unknown(other),
        }
    }
}

// ── Parsed packet ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct TracePacket {
    pub id: i32,
    pub event: EventCode,
    pub parent_id: i32,
    pub value_index: i32,
    pub type_: HalideType,
    /// Coordinates in dim-major / lane-minor order:
    /// [x₀..xₙ, y₀..yₙ, c₀..cₙ] where n = type_.lanes.
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

    /// Decodes lane `lane` of this packet's value into an `f64`. Returns `None`
    /// when the type isn't a decodable numeric (handle/unknown/odd bit width)
    /// or when the lane runs past the value bytes. All numeric types collapse to
    /// `f64` so callers have a single comparable scalar.
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
    pub max_store_count: i32,
    pub max_load_count: i32,
    pub max_redundant_count: i32,
    /// Frequency distribution of per-pixel store counts: `hist[k]` is the number of pixel
    /// locations stored exactly `k` times, for `k` in `0..=max_store_count`. The `0` bin is
    /// included. Empty when the Func has no usable extent.
    pub store_count_histogram: Vec<u32>,
    pub load_count_histogram: Vec<u32>,
    pub redundant_count_histogram: Vec<u32>,
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
            max_redundant_count: 0,
            store_count_histogram: vec![],
            load_count_histogram: vec![],
            redundant_count_histogram: vec![],
        }
    }
}

/// Full spatial layout of a Func: pixel dimensions plus the channel axis
/// (logical dim 2). Single source of truth for geometry, shared by the
/// renderer and by frontend-metadata derivation, so canvas sizing can never
/// disagree between them. All extents use the half-open `[min, max)`
/// convention that the coordinate accumulation establishes.
#[derive(Debug, Clone, Copy)]
pub struct FuncGeometry {
    pub width: usize,
    pub height: usize,
    pub channels: usize,
    pub min_x: i32,
    pub min_y: i32,
    pub min_c: i32,
    pub max_store_count: i32,
    pub max_load_count: i32,
    pub max_redundant_count: i32,
}

// ── Complete trace ────────────────────────────────────────────────────────────

// Note: We use BTreeMaps for deterministic iteration order here. We could
// consider switching to HashMaps to get O(1) lookups if we find funcs lookup
// start to become a bottleneck.
pub struct Trace {
    pub packets: Vec<TracePacket>,
    pub funcs: BTreeMap<String, FuncStats>,
    pub pipelines: BTreeMap<i32, String>,
    pub dag_edges: BTreeMap<String, BTreeSet<String>>,
    pub store_indices_by_func: BTreeMap<String, Vec<usize>>,
    pub load_indices_by_func: BTreeMap<String, Vec<usize>>,
}

// ── Binary parsing helpers ────────────────────────────────────────────────────

// halide_trace_packet_t fixed header: 7 × 4 bytes = 28 bytes.
//   u32  size        @ 0
//   i32  id          @ 4
//   u8   type.code   @ 8
//   u8   type.bits   @ 9
//   u16  type.lanes  @ 10
//   i32  event       @ 12
//   i32  parent_id   @ 16
//   i32  value_index @ 20
//   i32  dimensions  @ 24
//
// Immediately after the header:
//   i32  coordinates[dimensions]
//   u8   value[type.lanes * ceil(type.bits / 8)]
//   char func[]       (null-terminated)
//   char trace_tag[]  (null-terminated; empty string if absent)
const HEADER_BYTES: usize = 28;

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
    let null = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    (std::str::from_utf8(&buf[..null]).unwrap_or(""), null + 1)
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
                let c = pkt.coordinates[d * n_lanes + l];
                mn = mn.min(c);
                mx = mx.max(c + 1);
            }
            stats.min_coords[d] = mn;
            stats.max_coords[d] = mx;
        }
    } else {
        let dims = logical_dims.min(stats.min_coords.len());
        for d in 0..dims {
            for l in 0..n_lanes {
                let c = pkt.coordinates[d * n_lanes + l];
                if c < stats.min_coords[d] {
                    stats.min_coords[d] = c;
                }
                if c + 1 > stats.max_coords[d] {
                    stats.max_coords[d] = c + 1;
                }
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
                    stats.min_value = Some(v);
                    stats.max_value = Some(v);
                }
                (Some(mn), Some(mx)) => {
                    if v < mn {
                        stats.min_value = Some(v);
                    }
                    if v > mx {
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
    qualified: &str,
    trace_tag: &str,
    funcs: &mut BTreeMap<String, FuncStats>,
) {
    // Format: "func_type_and_dim: <num_types> [code bits lanes]{num_types}
    //                             <num_dims> [min extent]{num_dims}"
    let mut tokens = trace_tag.split_whitespace();
    tokens.next(); // consume "func_type_and_dim:"

    // Skip over the type descriptions, which we don't currently use.
    // We could consider using them in the future to populate FuncStats.type if that'd be a
    // useful addition.
    let num_types: usize = match tokens.next().and_then(|s| s.parse().ok()) {
        Some(n) => n,
        None => return,
    };
    for _ in 0..num_types * 3 {
        tokens.next();
    }

    // Parse the dimension descriptions to extract the overall min and max
    // coordinates for the Func.
    let num_dims: usize = match tokens.next().and_then(|s| s.parse().ok()) {
        Some(n) => n,
        None => return,
    };
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
    //path is "tag seeds, accesses expand."
    if !min_coords.is_empty() {
        let entry = funcs.entry(qualified.to_owned()).or_default();
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
        let mut pipelines: BTreeMap<i32, String> = BTreeMap::new();
        let mut dag_edges: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
        let mut store_indices_by_func: BTreeMap<String, Vec<usize>> = BTreeMap::new();
        let mut load_indices_by_func: BTreeMap<String, Vec<usize>> = BTreeMap::new();

        // id -> pipeline name: propagated down the parent chain so every event
        // in a pipeline can compute its qualified name.
        let mut parent_to_pipeline: HashMap<i32, String> = HashMap::new();
        // id -> (event, qualified_name, parent_id): needed for DAG inference after
        // all packets are parsed.
        let mut id_to_info: HashMap<i32, (EventCode, String, i32)> = HashMap::new();
        // Loads we deferred for DAG inference.
        let mut pending_loads: Vec<(String, i32)> = Vec::new();

        while pos + HEADER_BYTES <= total {
            let size = u32_le(data, pos) as usize;
            if size < HEADER_BYTES || pos + size > total {
                break;
            }

            // ── Fixed header fields ───────────────────────────────────────────
            let id = i32_le(data, pos + 4);
            let type_code = data[pos + 8];
            let type_bits = data[pos + 9];
            let type_lanes = u16_le(data, pos + 10);
            let event = i32_le(data, pos + 12);
            let parent_id = i32_le(data, pos + 16);
            let value_index = i32_le(data, pos + 20);
            let dimensions = i32_le(data, pos + 24) as usize;

            let type_ = HalideType {
                code: TypeCode::from_u8(type_code),
                bits: type_bits,
                lanes: type_lanes,
            };
            let ev = EventCode::from_i32(event);
            let pkt_data = &data[pos..pos + size];

            // ── Variable-length trailing fields ───────────────────────────────
            let coords_off = HEADER_BYTES;
            let value_off = coords_off + dimensions * 4;
            let value_len = type_.value_bytes();
            let func_off = value_off + value_len;

            let coords: Vec<i32> = (0..dimensions)
                .map(|i| i32_le(pkt_data, coords_off + i * 4))
                .collect();

            let value = if value_off + value_len <= pkt_data.len() {
                pkt_data[value_off..value_off + value_len].to_vec()
            } else {
                vec![]
            };

            let (func_name, func_len) = if func_off < pkt_data.len() {
                let (s, n) = read_cstr(&pkt_data[func_off..]);
                (s.to_owned(), n)
            } else {
                (String::new(), 0)
            };

            let tag_off = func_off + func_len;
            let trace_tag = if tag_off < pkt_data.len() {
                let (s, _) = read_cstr(&pkt_data[tag_off..]);
                s.to_owned()
            } else {
                String::new()
            };

            // ── Pipeline context propagation ──────────────────────────────────
            match ev {
                EventCode::BeginPipeline => {
                    pipelines.insert(id, func_name.clone());
                    parent_to_pipeline.insert(id, func_name.clone());
                }
                EventCode::EndPipeline => {
                    parent_to_pipeline.remove(&parent_id);
                }
                _ => {
                    if let Some(pl) = parent_to_pipeline.get(&parent_id).cloned() {
                        parent_to_pipeline.insert(id, pl);
                    }
                }
            }

            // ── Qualified name ────────────────────────────────────────────────
            let qualified = match parent_to_pipeline.get(&parent_id) {
                Some(pl) if !pl.is_empty() => format!("{}:{}", pl, func_name),
                _ => func_name.clone(),
            };
            id_to_info.insert(id, (ev, qualified.clone(), parent_id));

            // ── Build the packet ──────────────────────────────────────────────
            let pkt = TracePacket {
                id,
                event: ev,
                parent_id,
                value_index,
                type_,
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
                    parse_func_type_and_dim(&qualified, &trace_tag, &mut funcs);
                }
                EventCode::BeginRealization => {
                    funcs.entry(qualified.clone()).or_default();
                }
                EventCode::Load => {
                    // When we observe a load event, add its current index (equivalent to
                    // packets.len() before the push) to our BTreeMap of load indices for this Func.
                    load_indices_by_func
                        .entry(qualified.clone())
                        .or_default()
                        .push(packets.len());

                    // Add the load event to the list of pending loads to support DAG inference.
                    pending_loads.push((func_name.clone(), parent_id));
                    let stats = funcs.entry(qualified.clone()).or_default();

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
                        .entry(qualified.clone())
                        .or_default()
                        .push(packets.len());

                    // Update the min/max coordinate and value ranges for this Func based on the
                    // current store packet.
                    let stats = funcs.entry(qualified.clone()).or_default();
                    update_coord_range(&pkt, stats);
                    update_value_range(&pkt, stats);
                }
                _ => {}
            }

            packets.push(pkt);
            pos += size;
        }

        // ── DAG inference ─────────────────────────────────────────────────────
        // Walk up the parent chain from each load to find the enclosing Produce
        // event; that Produce's func is a producer of the loaded func.
        for (func_name, load_parent_id) in &pending_loads {
            let loaded_func = match parent_to_pipeline.get(load_parent_id) {
                Some(pl) if !pl.is_empty() => format!("{}:{}", pl, func_name),
                _ => func_name.clone(),
            };

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

        // Compute max per-pixel store/load counts for each Func using the qualified-name index
        // lists. We extract extents first (shared borrow) then write back (mut borrow) to keep the
        // two borrows of `funcs` non-overlapping.
        for (qualified, indices) in &store_indices_by_func {
            let extents = funcs.get(qualified.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let mut counts = vec![0i32; w * h];
                for &idx in indices {
                    let pkt = &packets[idx];
                    let n_lanes = pkt.type_.lanes.max(1) as usize;
                    let dims_per_lane = pkt.coordinates.len() / n_lanes;
                    for l in 0..n_lanes {
                        let (x, y) = pixel_xy(pkt, l, n_lanes, dims_per_lane, min_x, min_y);
                        if x >= 0 && y >= 0 && (x as usize) < w && (y as usize) < h {
                            counts[y as usize * w + x as usize] += 1;
                        }
                    }
                }
                if let Some(stats) = funcs.get_mut(qualified.as_str()) {
                    let (max, hist) = count_histogram(&counts);
                    stats.max_store_count = max;
                    stats.store_count_histogram = hist;
                }
            }
        }

        for (qualified, indices) in &load_indices_by_func {
            let extents = funcs.get(qualified.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let mut counts = vec![0i32; w * h];
                for &idx in indices {
                    let pkt = &packets[idx];
                    let n_lanes = pkt.type_.lanes.max(1) as usize;
                    let dims_per_lane = pkt.coordinates.len() / n_lanes;
                    for l in 0..n_lanes {
                        let (x, y) = pixel_xy(pkt, l, n_lanes, dims_per_lane, min_x, min_y);
                        if x >= 0 && y >= 0 && (x as usize) < w && (y as usize) < h {
                            counts[y as usize * w + x as usize] += 1;
                        }
                    }
                }
                if let Some(stats) = funcs.get_mut(qualified.as_str()) {
                    let (max, hist) = count_histogram(&counts);
                    stats.max_load_count = max;
                    stats.load_count_histogram = hist;
                }
            }
        }

        // Compute max per-pixel redundant store counts: replay all stores for each Func, tracking
        // the last value written to each (x, y, channel). A store is redundant when the incoming
        // value bit-matches the previously stored value at that location.
        for (qualified, indices) in &store_indices_by_func {
            let extents = funcs.get(qualified.as_str()).and_then(func_extents);
            if let Some((w, h, min_x, min_y)) = extents {
                let stats = funcs.get(qualified.as_str()).unwrap();
                let (channels, min_c) = if stats.min_coords.len() >= 3 {
                    (
                        (stats.max_coords[2] - stats.min_coords[2]).max(1) as usize,
                        stats.min_coords[2],
                    )
                } else {
                    (1, 0)
                };
                // None = no store has landed here yet; Some(bits) = last stored value as u64 bits.
                let mut last_values = vec![None::<u64>; w * h * channels];
                let mut redundant_counts = vec![0i32; w * h];
                for &idx in indices {
                    let pkt = &packets[idx];
                    let n_lanes = pkt.type_.lanes.max(1) as usize;
                    let dims_per_lane = pkt.coordinates.len() / n_lanes;
                    for lane in 0..n_lanes {
                        let Some(v) = pkt.decoded_value(lane) else {
                            continue;
                        };
                        let (x, y) = pixel_xy(pkt, lane, n_lanes, dims_per_lane, min_x, min_y);
                        if x < 0 || y < 0 || x as usize >= w || y as usize >= h {
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
                        let val_idx = (y as usize * w + x as usize) * channels + c as usize;
                        let pixel_idx = y as usize * w + x as usize;
                        let v_bits = v.to_bits();
                        if let Some(prev_bits) = last_values[val_idx] {
                            if prev_bits == v_bits {
                                redundant_counts[pixel_idx] += 1;
                            }
                        }
                        last_values[val_idx] = Some(v_bits);
                    }
                }
                if let Some(stats) = funcs.get_mut(qualified.as_str()) {
                    let (max, hist) = count_histogram(&redundant_counts);
                    stats.max_redundant_count = max;
                    stats.redundant_count_histogram = hist;
                }
            }
        }

        Ok(Self {
            packets,
            funcs,
            pipelines,
            dag_edges,
            store_indices_by_func,
            load_indices_by_func,
        })
    }

    // ── Render-path accessors ─────────────────────────────────────────────────

    /// Global packet indices of `qualified`'s store events, in ascending order. `None` if the Func
    /// emitted no stores. Use `partition_point(|&p| p <= g)` on the returned slice to turn a global
    /// timeline index `g` into the number of stores that have occurred by that point.
    pub fn func_store_indices(&self, qualified: &str) -> Option<&[usize]> {
        self.store_indices_by_func.get(qualified).map(Vec::as_slice)
    }

    pub fn func_load_indices(&self, qualified: &str) -> Option<&[usize]> {
        self.load_indices_by_func.get(qualified).map(Vec::as_slice)
    }

    /// Spatial layout for `qualified`, or `None` if it has no usable coordinate extent. Reuses
    /// `func_extents` for pixel dims so the renderer and the metadata layer agree, and adds the
    /// channel axis (logical dim 2).
    pub fn func_geometry(&self, qualified: &str) -> Option<FuncGeometry> {
        let stats = self.funcs.get(qualified)?;
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
            max_redundant_count: stats.max_redundant_count,
        })
    }
}

// ── Shared geometry helpers ───────────────────────────────────────────────────

/// Builds a frequency histogram from per-pixel `counts`: `hist[k]` is the number of pixel
/// locations whose count is exactly `k`, for `k` in `0..=max`. The `0` bin is included so the
/// frontend can surface untouched locations. Counts are always non-negative. Returns
/// `(max_count, hist)`.
fn count_histogram(counts: &[i32]) -> (i32, Vec<u32>) {
    let max = counts.iter().copied().max().unwrap_or(0);
    let mut hist = vec![0u32; max as usize + 1];
    for &c in counts {
        hist[c as usize] += 1;
    }
    (max, hist)
}

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
