//! Frontend-facing API contract for Halidoscope.
//!
//! This module owns the types that cross the Tauri IPC boundary.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use tauri::ipc::Response;
use tauri::{AppHandle, Emitter, State};

use crate::render::{
    GrayscaleState, IncludeInf, IncludeNan, LoadFrequencyState, NormalizationMode, RedundantState,
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
    pub thread_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
pub struct StatsMeta {
    global_max_store_count: u32,
    global_max_load_count: u32,
    global_max_redundant_store_count: u32,
    global_max_reuse_distance: u64,
    global_thread_ids: Vec<String>,
}

/// Top-level payload returned by `open_trace`.
#[derive(Debug, Clone, Serialize)]
pub struct TraceMeta {
    pub funcs: Vec<FuncMeta>,
    pub total_packets: u32,
    pub dag_edges: BTreeMap<String, Vec<String>>,
    pub stats: StatsMeta,
}

impl TraceMeta {
    pub fn from_trace(trace: &Trace) -> Self {
        let mut global_max_store_count = 0u32;
        let mut global_max_load_count = 0u32;
        let mut global_max_redundant_store_count = 0u32;
        let mut global_max_reuse_distance = 0u64;
        let mut global_thread_ids: BTreeSet<i32> = BTreeSet::new();

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

                global_max_store_count = stats.max_store_count.max(global_max_store_count);
                global_max_load_count = stats.max_load_count.max(global_max_load_count);
                global_max_redundant_store_count = stats
                    .max_redundant_store_count
                    .max(global_max_redundant_store_count);
                global_max_reuse_distance = stats.max_reuse_distance.max(global_max_reuse_distance);

                if let Some(thread_ids) = trace.func_thread_ids(name) {
                    global_thread_ids.extend(thread_ids);
                }

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
                        .map(|ids| ids.into_iter().map(|x| x.to_string()).collect())
                        .unwrap_or_else(|| vec![]),
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
            stats: StatsMeta {
                global_max_store_count,
                global_max_load_count,
                global_max_redundant_store_count,
                global_max_reuse_distance,
                global_thread_ids: global_thread_ids
                    .into_iter()
                    .map(|id| id.to_string())
                    .collect(),
            },
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
    thread_renderers: HashMap<String, ThreadState>,
}

/// App-wide state managed by Tauri. A single trace is loaded at a time; opening a new one replaces
/// it (and drops all stale render caches).
#[derive(Default)]
pub struct AppState {
    inner: Mutex<Option<Loaded>>,
}

/// Packs tensor data, tabular data, and NaN / Inf data in a single IPC response.
fn pack_render_response(
    mut pixels: Vec<u8>,
    nan_inf_overlays: Vec<u8>,
    tabular_data: &[u32],
) -> Vec<u8> {
    pixels.reserve(nan_inf_overlays.len() + tabular_data.len() * 4);

    pixels.extend_from_slice(&nan_inf_overlays);

    for &v in tabular_data {
        pixels.extend_from_slice(&v.to_le_bytes());
    }

    pixels
}

// ── Commands ──────────────────────────────────────────────────────────────────

/// Parses a `.hltrace` file and returns the metadata the frontend needs to set up canvases and
/// the scrub timeline. Replaces any previously loaded trace.
///
/// Runs the parse on a blocking-task thread rather than the main thread: `open_trace` isn't
/// declared `async`, so a plain `fn` command would otherwise execute inline on the thread that
/// pumps the webview's event loop, freezing the UI (including any in-progress loading indicator)
/// for the duration of the parse. Progress (percentage of bytes parsed) is emitted to the
/// frontend as `trace-load-progress` events.
#[tauri::command]
pub async fn open_trace(
    path: String,
    app: AppHandle,
    state: State<'_, AppState>,
) -> Result<TraceMeta, String> {
    let (trace, meta) = tauri::async_runtime::spawn_blocking(move || {
        let trace = Trace::load_from_file(&path, |pct| {
            let _ = app.emit("trace-load-progress", pct);
        })?;
        let meta = TraceMeta::from_trace(&trace);
        Ok::<_, String>((trace, meta))
    })
    .await
    .map_err(|e| e.to_string())??;

    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    *guard = Some(Loaded {
        trace,
        grayscale_renderers: HashMap::new(),
        rgb_renderers: HashMap::new(),
        store_frequency_renderers: HashMap::new(),
        load_frequency_renderers: HashMap::new(),
        redundant_renderers: HashMap::new(),
        reuse_distance_renderers: HashMap::new(),
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
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = if include_tabular_data {
        renderer.to_histogram()
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

/// Renders `func` as an RGB image at `global_index` and returns raw RGBA8 bytes. Planes 0/1/2
/// map to R/G/B; missing planes default to 0.
#[tauri::command]
pub fn render_rgb(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = if include_tabular_data {
        renderer.to_histogram()
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

/// Renders a heatmap of store counts for `func` up to `global_index` and returns raw RGBA8 bytes.
#[tauri::command]
pub fn render_store_frequency(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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
    let histogram = if include_tabular_data {
        renderer.to_tabular_data(normalization_mode)
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

/// Renders a heatmap of load counts for `func` up to `global_index` and returns raw RGBA8 bytes.
#[tauri::command]
pub fn render_load_frequency(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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
    let histogram = if include_tabular_data {
        renderer.to_tabular_data(normalization_mode)
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);

    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

/// Renders a heatmap of redundant store counts for `func` up to `global_index` and returns raw
/// RGBA8 bytes. A store is redundant when it writes the same value to a location that already
/// holds that value _and_ no intervening load has read that value.
#[tauri::command]
pub fn render_redundant_stores(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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
    let load_indices = trace.func_load_indices(&func).unwrap_or(&[]);
    let store_k = store_indices.partition_point(|&p| p <= global_index as usize);
    let load_k = load_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, load_indices, store_k, load_k);

    let pixels = renderer.to_rgba(normalization_mode);
    let histogram = if include_tabular_data {
        renderer.to_tabular_data(normalization_mode)
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

/// Renders a heatmap of maximum store-to-load reuse distances for `func` up to `global_index`
/// and returns raw RGBA8 bytes. Reuse distance is the number of packets elapsed between a store
/// and the next load from the same (x, y, channel).
#[tauri::command]
pub fn render_reuse_distance(
    func: String,
    global_index: u32,
    normalization_mode: NormalizationMode,
    include_tabular_data: bool,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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
    let histogram = if include_tabular_data {
        renderer.to_tabular_data(normalization_mode)
    } else {
        Vec::new()
    };
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &histogram,
    )))
}

#[tauri::command]
pub fn render_thread(
    func: String,
    global_index: u32,
    op_mode: ThreadOpMode,
    thread_id: String,
    include_nan: IncludeNan,
    include_inf: IncludeInf,
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

    let pixels = renderer.to_rgba(thread_id);
    let (store_counts, load_counts) = renderer.to_thread_counts();
    let thread_counts: Vec<u32> = store_counts.iter().chain(load_counts).copied().collect();
    let nan_inf_overlays = renderer.to_nan_inf_overlay(include_nan, include_inf);
    Ok(Response::new(pack_render_response(
        pixels,
        nan_inf_overlays,
        &thread_counts,
    )))
}

// ── Profiler ─────────────────────────────────────────────────────────────────────────────────────
#[derive(Debug, Clone, Serialize, Deserialize)]
struct ProfileFunc {
    name: String,
    parent: i32,
    canonical_id: u32,
    kind: u32,
    buffer_func_id: i32,
    time_ns: u64,
    memory_current: u64,
    memory_peak: u64,
    memory_total: u64,
    stack_peak: u64,
    active_threads_numerator: u32,
    active_threads_denominator: u32,
    num_allocs: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct ProfilePipeline {
    name: String,
    runs: u32,
    billed_runs: u32,
    samples: u32,
    num_allocs: u32,
    time_ns: u64,
    memory_current: u64,
    memory_peak: u64,
    memory_total: u64,
    active_threads_numerator: u32,
    active_threads_denominator: u32,
    funcs: Vec<ProfileFunc>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Profile {
    pipelines: Vec<ProfilePipeline>,
}

impl Profile {
    fn from_profile(path: &str) -> Result<Profile, String> {
        let data = std::fs::read(path).map_err(|e| e.to_string())?;
        serde_json::from_slice(&data).map_err(|e| e.to_string())
    }
}

#[tauri::command]
pub fn open_profile(path: &str) -> Result<Profile, String> {
    let profile = Profile::from_profile(path)?;

    Ok(profile)
}
