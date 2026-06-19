//! Frontend-facing API contract for Halidoscope.
//!
//! This module owns the types that cross the Tauri IPC boundary.

use std::collections::{BTreeMap, HashMap};
use std::sync::Mutex;

use serde::{Deserialize, Serialize};
use tauri::ipc::Response;
use tauri::State;

use crate::render::{HeatmapState, RedundantState, RenderState};
use crate::trace::Trace;

/// How a Func's values are mapped to pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RenderMode {
    Grayscale,
    Rgb,
}

/// Which access type to visualize in a heatmap render. Variant names match the frontend's
/// `VisualizationMode` strings so they can be passed through without conversion.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum HeatmapMode {
    #[serde(rename = "Store Frequency")]
    Stores,
    #[serde(rename = "Load Frequency")]
    Loads,
}

/// The default render mode inferred from a Func's channel count: 3 or 4 channels are treated as
/// color, everything else as grayscale. Shared by the metadata derivation and `render_at` so the
/// inferred default never diverges.
pub fn default_mode(channels: u32) -> RenderMode {
    if channels == 3 || channels == 4 {
        RenderMode::Rgb
    } else {
        RenderMode::Grayscale
    }
}

/// Per-Func metadata the frontend needs to size canvases and bound the scrub timeline.
#[derive(Debug, Clone, Serialize)]
pub struct FuncMeta {
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub channels: u32,
    pub default_mode: RenderMode,
    /// Number of store events for this Func across the whole trace.
    pub num_stores: u32,
    /// Per-dimension coordinate extent, half-open `[min, max)`. Surfaced for the
    /// funcs inspector panel.
    pub min_coords: Vec<i32>,
    pub max_coords: Vec<i32>,
    pub min_value: Option<f64>,
    pub max_value: Option<f64>,
    pub max_store_count: i32,
    pub max_load_count: i32,
    pub max_redundant_count: i32,
    /// Frequency distributions of per-pixel counts, indexed by count value (the `0` bin is
    /// included). Length is the corresponding `max_*_count + 1`; empty when the Func has no
    /// usable extent. Rendered directly as histograms by the frontend.
    pub store_count_histogram: Vec<u32>,
    pub load_count_histogram: Vec<u32>,
    pub redundant_count_histogram: Vec<u32>,
}

/// Top-level payload returned by `open_trace`.
#[derive(Debug, Clone, Serialize)]
pub struct TraceMeta {
    pub funcs: Vec<FuncMeta>,
    pub total_packets: u32,
    pub dag_edges: BTreeMap<String, Vec<String>>,
    pub global_max_store_count: i32,
    pub global_max_load_count: i32,
    pub global_max_redundant_count: i32,
}

impl TraceMeta {
    /// Derives the frontend contract from a parsed trace. Funcs with no usable coordinate extent
    /// are still listed (with zero dimensions) so the UI can surface them; the renderer simply
    /// produces nothing for them.
    pub fn from_trace(trace: &Trace) -> Self {
        let mut global_max_store_count = 0;
        let mut global_max_load_count = 0;
        let mut global_max_redundant_count = 0;

        let funcs = trace
            .funcs
            .iter()
            .map(|(name, stats)| {
                let geom = trace.func_geometry(name);
                let (width, height, channels) = match geom {
                    Some(g) => (g.width as u32, g.height as u32, g.channels as u32),
                    None => (0, 0, 1),
                };
                let default_mode = default_mode(channels);
                let stores = trace.func_store_indices(name);
                let num_stores = stores.map(<[usize]>::len).unwrap_or(0) as u32;

                if stats.max_store_count > global_max_store_count {
                    global_max_store_count = stats.max_store_count;
                }
                if stats.max_load_count > global_max_load_count {
                    global_max_load_count = stats.max_load_count;
                }
                if stats.max_redundant_count > global_max_redundant_count {
                    global_max_redundant_count = stats.max_redundant_count;
                }

                FuncMeta {
                    name: name.clone(),
                    width,
                    height,
                    channels,
                    default_mode,
                    num_stores,
                    min_coords: stats.min_coords.clone(),
                    max_coords: stats.max_coords.clone(),
                    min_value: stats.min_value,
                    max_value: stats.max_value,
                    max_store_count: stats.max_store_count,
                    max_load_count: stats.max_load_count,
                    max_redundant_count: stats.max_redundant_count,
                    store_count_histogram: stats.store_count_histogram.clone(),
                    load_count_histogram: stats.load_count_histogram.clone(),
                    redundant_count_histogram: stats.redundant_count_histogram.clone(),
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
            global_max_store_count,
            global_max_load_count,
            global_max_redundant_count,
        }
    }
}

// ── Tauri-managed state ───────────────────────────────────────────────────────

/// The currently loaded trace plus a per-Func render cache. The cache keeps each Func's
/// framebuffer warm across requests so forward scrubbing only applies the delta of new stores.
struct Loaded {
    trace: Trace,
    renderers: HashMap<String, RenderState>,
    heatmap_renderers: HashMap<String, HeatmapState>,
    redundant_renderers: HashMap<String, RedundantState>,
}

/// App-wide state managed by Tauri. A single trace is loaded at a time; opening a new one replaces
/// it (and drops the stale render cache).
#[derive(Default)]
pub struct AppState {
    inner: Mutex<Option<Loaded>>,
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
        renderers: HashMap::new(),
        heatmap_renderers: HashMap::new(),
        redundant_renderers: HashMap::new(),
    });
    Ok(meta)
}

/// Renders `func`'s framebuffer state at global timeline position `global_index` and returns it
/// as raw RGBA8 bytes (delivered to the frontend as an `ArrayBuffer`, bypassing JSON). `mode`
/// overrides the Func's inferred default when provided. Both scrubbing and playback drive this
/// single command.
#[tauri::command]
pub fn render_at(
    func: String,
    global_index: u32,
    mode: Option<RenderMode>,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    // Split the borrow so the trace can be read while a renderer is mutated.
    let Loaded {
        trace, renderers, ..
    } = loaded;

    // Get or lazily build this Func's render state.
    if !renderers.contains_key(&func) {
        let rs = RenderState::new(trace, &func)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        renderers.insert(func.clone(), rs);
    }
    let renderer = renderers.get_mut(&func).expect("just inserted");

    // Resolve the global timeline index into a store count: how many of this Func's stores have
    // occurred by `global_index` (inclusive).
    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    renderer.seek(trace, store_indices, k);

    let mode = mode.unwrap_or_else(|| default_mode(renderer.channels() as u32));
    Ok(Response::new(renderer.to_rgba(mode)))
}

/// Renders a heatmap of store or load counts for `func` up to `global_index` and returns raw RGBA8
/// bytes. Mirrors `render_at` — forward seeks apply only the new events; backward seeks clear and
/// replay. Counts are normalized against the per-Func full-trace maximum so the color scale is stable.
#[tauri::command]
pub fn render_heatmap(
    func: String,
    global_index: u32,
    mode: HeatmapMode,
    state: State<AppState>,
) -> Result<Response, String> {
    let mut guard = state.inner.lock().map_err(|e| e.to_string())?;
    let loaded = guard.as_mut().ok_or("no trace loaded")?;
    let Loaded {
        trace,
        heatmap_renderers,
        ..
    } = loaded;

    if !heatmap_renderers.contains_key(&func) {
        let hs = HeatmapState::new(trace, &func, mode)
            .ok_or_else(|| format!("func '{func}' has no renderable geometry"))?;
        heatmap_renderers.insert(func.clone(), hs);
    }
    let hs = heatmap_renderers.get_mut(&func).expect("just inserted");

    let event_indices = match mode {
        HeatmapMode::Stores => trace.func_store_indices(&func).unwrap_or(&[]),
        HeatmapMode::Loads => trace.func_load_indices(&func).unwrap_or(&[]),
    };
    let k = event_indices.partition_point(|&p| p <= global_index as usize);
    hs.seek(trace, event_indices, k, mode);

    Ok(Response::new(hs.to_rgba()))
}

/// Renders a heatmap of redundant store counts for `func` up to `global_index` and returns raw
/// RGBA8 bytes. A store is redundant when it writes the same value to a location that already holds
/// that value. Counts are normalized against the per-Func full-trace maximum so the scale is stable
/// while scrubbing. Pixels with zero redundant stores are black; positive counts map through the
/// Reds colormap.
#[tauri::command]
pub fn render_redundant(
    func: String,
    global_index: u32,
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
    let rs = redundant_renderers.get_mut(&func).expect("just inserted");

    let store_indices = trace.func_store_indices(&func).unwrap_or(&[]);
    let k = store_indices.partition_point(|&p| p <= global_index as usize);
    rs.seek(trace, store_indices, k);

    Ok(Response::new(rs.to_rgba()))
}
