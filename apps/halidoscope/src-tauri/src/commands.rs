//! Frontend-facing API contract for Halidoscope.
//!
//! This module owns the types that cross the Tauri IPC boundary.

use std::collections::{BTreeMap, HashMap};
use std::sync::Mutex;

use serde::Serialize;
use tauri::ipc::Response;
use tauri::State;

use crate::render::{
    GrayscaleState, InfState, LoadFrequencyState, NaNState, NormalizationMode, RedundantState,
    Renderer, ReuseDistanceState, RgbState, StoreFrequencyState, ThreadOpMode, ThreadState,
};
use crate::trace::Trace;

/// A half-open packet-index interval `[start, end]` used for liveness and produce/consume ranges.
#[derive(Debug, Clone, Copy, Serialize)]
pub struct IndexRange {
    pub start: u32,
    pub end: u32,
}

impl IndexRange {
    fn from_tuple((start, end): (u32, u32)) -> Self {
        Self { start, end }
    }
}

/// Per-Func metadata the frontend needs to size canvases and bound the scrub timeline.
#[derive(Debug, Clone, Serialize)]
pub struct FuncMeta {
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub channels: u32,
    pub num_stores: u32,
    pub min_coords: Vec<i32>,
    pub max_coords: Vec<i32>,
    pub min_value: Option<f64>,
    pub max_value: Option<f64>,
    pub max_store_count: u32,
    pub max_load_count: u32,
    pub max_redundant_store_count: u32,
    pub max_reuse_distance: u64,
    pub buffer_liveness: IndexRange,
    pub produce_ranges: Vec<IndexRange>,
    pub consume_ranges: Vec<IndexRange>,
    pub thread_count: u32,
    pub thread_ids: Vec<i32>,
}

/// Top-level payload returned by `open_trace`.
#[derive(Debug, Clone, Serialize)]
pub struct TraceMeta {
    pub funcs: Vec<FuncMeta>,
    pub total_packets: u32,
    pub dag_edges: BTreeMap<String, Vec<String>>,
    pub global_max_reuse_distance: u64,
}

impl TraceMeta {
    /// Derives the frontend contract from a parsed trace. Funcs with no usable coordinate extent
    /// are still listed (with zero dimensions) so the UI can surface them; the renderer simply
    /// produces nothing for them.
    pub fn from_trace(trace: &Trace) -> Self {
        let mut global_max_reuse_distance = 0u64;

        let funcs = trace
            .funcs
            .iter()
            .map(|(name, stats)| {
                let geom = trace.func_geometry(name);
                let (width, height, channels) = match geom {
                    Some(g) => (g.width as u32, g.height as u32, g.channels as u32),
                    None => (0, 0, 1),
                };
                let stores = trace.func_store_indices(name);
                let num_stores = stores.map(<[usize]>::len).unwrap_or(0) as u32;

                global_max_reuse_distance = stats.max_reuse_distance.max(global_max_reuse_distance);

                FuncMeta {
                    name: name.clone(),
                    width,
                    height,
                    channels,
                    num_stores,
                    min_coords: stats.min_coords.clone(),
                    max_coords: stats.max_coords.clone(),
                    min_value: stats.min_value,
                    max_value: stats.max_value,
                    max_store_count: stats.max_store_count,
                    max_load_count: stats.max_load_count,
                    max_redundant_store_count: stats.max_redundant_store_count,
                    max_reuse_distance: stats.max_reuse_distance,
                    buffer_liveness: IndexRange::from_tuple(
                        trace
                            .func_buffer_liveness_range(name)
                            .unwrap_or(&(0, 0))
                            .clone(),
                    ),
                    produce_ranges: trace
                        .func_produce_ranges(name)
                        .unwrap_or(&[])
                        .iter()
                        .copied()
                        .map(IndexRange::from_tuple)
                        .collect(),
                    consume_ranges: trace
                        .func_consume_ranges(name)
                        .unwrap_or(&[])
                        .iter()
                        .copied()
                        .map(IndexRange::from_tuple)
                        .collect(),
                    // A missing entry means `name` ran entirely serially (never inside a
                    // `BeginParallelTask`), not that it has no threads; default to the implicit
                    // serial thread `{0}` so `thread_ids` and `thread_count` agree.
                    thread_count: trace
                        .func_thread_ids(name)
                        .map_or(1, |ids| ids.len() as u32),
                    thread_ids: trace
                        .func_thread_ids(name)
                        .map(|ids| ids.iter().copied().collect())
                        .unwrap_or_else(|| vec![0]),
                }
            })
            .collect();

        let dag_edges = trace
            .dag_edges
            .iter()
            .map(|(consumer, producers)| (consumer.clone(), producers.iter().cloned().collect()))
            .collect();

        TraceMeta {
            funcs,
            total_packets: trace.packets.len() as u32,
            dag_edges,
            global_max_reuse_distance,
        }
    }
}

// ── Tauri-managed state ───────────────────────────────────────────────────────

/// The currently loaded trace plus per-Func render caches, one map per rendering pathway. Each
/// cache keeps its Func's state warm across requests so forward scrubbing only applies the delta.
struct Loaded {
    trace: Trace,
    grayscale_renderers: HashMap<String, GrayscaleState>,
    rgb_renderers: HashMap<String, RgbState>,
    store_frequency_renderers: HashMap<String, StoreFrequencyState>,
    load_frequency_renderers: HashMap<String, LoadFrequencyState>,
    redundant_renderers: HashMap<String, RedundantState>,
    reuse_distance_renderers: HashMap<String, ReuseDistanceState>,
    nan_renderers: HashMap<String, NaNState>,
    inf_renderers: HashMap<String, InfState>,
    thread_renderers: HashMap<String, ThreadState>,
}

/// App-wide state managed by Tauri. A single trace is loaded at a time; opening a new one replaces
/// it (and drops all stale render caches).
#[derive(Default)]
pub struct AppState {
    inner: Mutex<Option<Loaded>>,
}

/// Appends `histogram`'s bins as little-endian `u32`s directly after `pixels`, so a single
/// `Response` carries both. The frontend already knows the pixel-buffer length ahead of time
/// (`width * height * 4`), so no length prefix is needed to split the two back apart.
fn pack_pixels_and_histogram(mut pixels: Vec<u8>, histogram: Vec<u32>) -> Vec<u8> {
    pixels.reserve(histogram.len() * 4);
    for bin in histogram {
        pixels.extend_from_slice(&bin.to_le_bytes());
    }
    pixels
}

/// Appends `store_counts` then `load_counts` as little-endian `u32`s directly after `pixels`.
/// Both slices are the same length (the Func's thread-ID domain size, `FuncMeta::thread_ids`), so
/// the frontend can split them back apart without a length prefix.
fn pack_pixels_and_thread_counts(
    mut pixels: Vec<u8>,
    store_counts: &[u32],
    load_counts: &[u32],
) -> Vec<u8> {
    pixels.reserve((store_counts.len() + load_counts.len()) * 4);
    for &c in store_counts {
        pixels.extend_from_slice(&c.to_le_bytes());
    }
    for &c in load_counts {
        pixels.extend_from_slice(&c.to_le_bytes());
    }
    pixels
}

// ── Commands ──────────────────────────────────────────────────────────────────

/// Parses a `.hltrace` file and returns the metadata the frontend needs to set up canvases and
/// the scrub timeline. Replaces any previously loaded trace.
#[tauri::command]
pub fn open_trace(path: String, state: State<AppState>) -> Result<TraceMeta, String> {
    let trace = Trace::load_from_file(&path)?;
    let meta = TraceMeta::from_trace(&trace);

    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    *guard = Some(Loaded {
        trace,
        grayscale_renderers: HashMap::new(),
        rgb_renderers: HashMap::new(),
        store_frequency_renderers: HashMap::new(),
        load_frequency_renderers: HashMap::new(),
        redundant_renderers: HashMap::new(),
        reuse_distance_renderers: HashMap::new(),
        nan_renderers: HashMap::new(),
        inf_renderers: HashMap::new(),
        thread_renderers: HashMap::new(),
    });
    Ok(meta)
}

/// Renders `func` as a grayscale image at `global_index` and returns raw RGBA8 bytes. Channel 0
/// is normalized to [0, 255] and replicated across R/G/B.
#[tauri::command]
pub fn render_grayscale(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        grayscale_renderers,
        ..
    } = loaded;

    if !grayscale_renderers.contains_key(&func) {
        let rs = GrayscaleState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        grayscale_renderers.insert(func.clone(), rs);
    }
    let renderer = grayscale_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    Ok(Response::new(renderer.to_rgba(normalization_mode)))
}

/// Renders `func` as an RGB image at `global_index` and returns raw RGBA8 bytes. Planes 0/1/2
/// map to R/G/B; missing planes default to 0.
#[tauri::command]
pub fn render_rgb(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        rgb_renderers,
        ..
    } = loaded;

    if !rgb_renderers.contains_key(&func) {
        let rs = RgbState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        rgb_renderers.insert(func.clone(), rs);
    }
    let renderer = rgb_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    Ok(Response::new(renderer.to_rgba(normalization_mode)))
}

/// Renders a heatmap of store counts for `func` up to `global_index` and returns raw RGBA8 bytes.
#[tauri::command]
pub fn render_store_frequency(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        store_frequency_renderers,
        ..
    } = loaded;

    if !store_frequency_renderers.contains_key(&func) {
        let hs = StoreFrequencyState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        store_frequency_renderers.insert(func.clone(), hs);
    }
    let renderer = store_frequency_renderers
        .get_mut(&func)
        .expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = renderer.to_histogram(normalization_mode);
    Ok(Response::new(pack_pixels_and_histogram(pixels, histogram)))
}

/// Renders a heatmap of load counts for `func` up to `global_index` and returns raw RGBA8 bytes.
#[tauri::command]
pub fn render_load_frequency(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        load_frequency_renderers,
        ..
    } = loaded;

    if !load_frequency_renderers.contains_key(&func) {
        let hs = LoadFrequencyState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        load_frequency_renderers.insert(func.clone(), hs);
    }
    let renderer = load_frequency_renderers
        .get_mut(&func)
        .expect("just inserted");

    let load_indices = trace.func_load_indices(&func).unwrap_or(&[]);
    let k = load_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, load_indices, k);

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = renderer.to_histogram(normalization_mode);
    Ok(Response::new(pack_pixels_and_histogram(pixels, histogram)))
}

/// Renders a heatmap of redundant store counts for `func` up to `global_index` and returns raw
/// RGBA8 bytes. A store is redundant when it writes the same value to a location that already
/// holds that value _and_ no intervening load has read that value.
#[tauri::command]
pub fn render_redundant_stores(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        redundant_renderers,
        ..
    } = loaded;

    if !redundant_renderers.contains_key(&func) {
        let rs = RedundantState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        redundant_renderers.insert(func.clone(), rs);
    }
    let renderer = redundant_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = renderer.to_histogram(normalization_mode);
    Ok(Response::new(pack_pixels_and_histogram(pixels, histogram)))
}

/// Renders a heatmap of maximum store-to-load reuse distances for `func` up to `global_index`
/// and returns raw RGBA8 bytes. Reuse distance is the number of packets elapsed between a store
/// and the next load from the same (x, y, channel).
#[tauri::command]
pub fn render_reuse_distance(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        reuse_distance_renderers,
        ..
    } = loaded;

    if !reuse_distance_renderers.contains_key(&func) {
        let rs = ReuseDistanceState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        reuse_distance_renderers.insert(func.clone(), rs);
    }
    let renderer = reuse_distance_renderers
        .get_mut(&func)
        .expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let load_indices = trace.func_load_indices(&func).unwrap_or(&[]);
    let store_k = store_indices.partition_point(|&p| p <= global_index as usize);
    let load_k = load_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, load_indices, store_k, load_k);

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = renderer.to_histogram(normalization_mode);
    Ok(Response::new(pack_pixels_and_histogram(pixels, histogram)))
}

#[tauri::command]
pub fn render_nan(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        nan_renderers,
        ..
    } = loaded;

    if !nan_renderers.contains_key(&func) {
        let rs = NaNState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        nan_renderers.insert(func.clone(), rs);
    }
    let renderer = nan_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    Ok(Response::new(renderer.to_rgba(normalization_mode)))
}

#[tauri::command]
pub fn render_inf(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        inf_renderers,
        ..
    } = loaded;

    if !inf_renderers.contains_key(&func) {
        let rs = InfState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        inf_renderers.insert(func.clone(), rs);
    }
    let renderer = inf_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    Ok(Response::new(renderer.to_rgba(normalization_mode)))
}

#[tauri::command]
pub fn render_thread(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    op_mode: ThreadOpMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        thread_renderers,
        ..
    } = loaded;

    if !thread_renderers.contains_key(&func) {
        let rs = ThreadState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        thread_renderers.insert(func.clone(), rs);
    }
    let renderer = thread_renderers.get_mut(&func).expect("just inserted");
    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let load_indices = trace.func_load_indices(&func).unwrap_or(&[]);
    let store_k = store_indices.partition_point(|&p| p <= global_index as usize);
    let load_k = load_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, load_indices, store_k, load_k, op_mode);

    let pixels = renderer.to_rgba(normalization_mode);
    let (store_counts, load_counts) = renderer.to_thread_counts();
    Ok(Response::new(pack_pixels_and_thread_counts(
        pixels,
        store_counts,
        load_counts,
    )))
}
