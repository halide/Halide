use std::ffi::OsStr;
use std::path::Path;

use image;
use tauri_plugin_cli::SubcommandMatches;

use crate::graph::to_dot;
use crate::render::{
    GrayscaleState, LoadFrequencyState, RedundantState, Renderer, ReuseDistanceState, RgbState,
    StoreFrequencyState,
};
use crate::trace::{func_extents, Trace};

pub fn halidoscope_cli(subcommand: Box<SubcommandMatches>) {
    match subcommand.name.as_str() {
        "snapshot" => {
            let args = &subcommand.matches.args;

            let trace = args
                .get("trace")
                .and_then(|a| a.value.as_str())
                .unwrap_or_else(|| {
                    eprintln!("Error: --trace argument is required.");
                    std::process::exit(1);
                });
            let func = args
                .get("func")
                .and_then(|a| a.value.as_str())
                .unwrap_or_else(|| {
                    eprintln!("Error: --func argument is required.");
                    std::process::exit(1);
                });
            let packet_index = args
                .get("packet-index")
                .and_then(|a| a.value.as_str())
                .and_then(|s| s.parse::<u32>().ok())
                .unwrap_or(0);
            let mode = args
                .get("mode")
                .and_then(|a| a.value.as_str())
                .unwrap_or("grayscale");
            let destination = args
                .get("destination")
                .and_then(|a| a.value.as_str())
                .unwrap_or_else(|| {
                    eprintln!("Error: A destination for the snapshot is required.");
                    std::process::exit(1);
                });

            // Load and parse the trace.
            let tr = Trace::load_from_file(trace).unwrap_or_else(|e| {
                eprintln!("Error loading trace: {}", e);
                std::process::exit(1);
            });

            // Find the target function.
            let target_func = tr.funcs.get(func).unwrap_or_else(|| {
                eprintln!(
                    "Func '{}' not found in trace. Available Funcs: {:?}",
                    func,
                    tr.funcs.keys().collect::<Vec<_>>()
                );
                std::process::exit(1);
            });

            // Exit early if the packet index is out of bounds.
            if packet_index as usize >= tr.packets.len() {
                eprintln!(
                    "Packet index {} is out of bounds. Valid range: 0..{}",
                    packet_index,
                    tr.packets.len()
                );
                std::process::exit(1);
            }

            let store_indices = tr.func_store_indices(func).unwrap_or(&[]);
            let load_indices = tr.func_load_indices(func).unwrap_or(&[]);

            let buffer = match mode {
                "grayscale" => {
                    write_buffer::<GrayscaleState>(&tr, func, store_indices, packet_index)
                }
                "rgb" => write_buffer::<RgbState>(&tr, func, store_indices, packet_index),
                "store-frequency" => {
                    write_buffer::<StoreFrequencyState>(&tr, func, store_indices, packet_index)
                }
                "load-frequency" => {
                    write_buffer::<LoadFrequencyState>(&tr, func, load_indices, packet_index)
                }
                "redundant-stores" => {
                    write_buffer::<RedundantState>(&tr, func, store_indices, packet_index)
                }
                "reuse-distance" => write_reuse_distance_buffer(
                    &tr,
                    func,
                    store_indices,
                    load_indices,
                    packet_index,
                ),
                _ => {
                    eprintln!("Unknown rendering mode: {}", mode);
                    std::process::exit(1);
                }
            };

            if let Some((width, height, _, _)) = func_extents(target_func) {
                match image::save_buffer(
                    &destination,
                    &buffer,
                    width as u32,
                    height as u32,
                    image::ColorType::Rgba8,
                ) {
                    Ok(_) => {
                        println!("Snapshot written to {}", destination);
                        std::process::exit(0);
                    }
                    Err(e) => {
                        eprintln!("Error saving snapshot: {}", e);
                        std::process::exit(1);
                    }
                }
            }

            // If we reach here, it means we couldn't determine the dimensions of the Func.
            eprintln!(
                "Could not determine dimensions for Func '{}'. Ensure it has valid geometry.",
                func
            );
            std::process::exit(1);
        }
        "dot" => {
            let args = &subcommand.matches.args;

            let trace = args
                .get("trace")
                .and_then(|a| a.value.as_str())
                .unwrap_or_else(|| {
                    eprintln!("Error: --trace argument is required.");
                    std::process::exit(1);
                });
            let destination = args.get("destination").and_then(|a| a.value.as_str());

            // Load and parse the trace.
            let tr = Trace::load_from_file(trace).unwrap_or_else(|e| {
                eprintln!("Error loading trace: {}", e);
                std::process::exit(1);
            });
            let dot = to_dot(&tr.dag_edges);

            match destination {
                Some(dest) => {
                    let ext = Path::new(dest).extension().and_then(OsStr::to_str);

                    match ext {
                        Some("txt") | Some("gv") | Some("dot") => {
                            if let Err(e) = std::fs::write(dest, &dot) {
                                eprintln!("Failed to write DOT file: {}", e);
                                std::process::exit(1);
                            }

                            println!("DOT file written to {}", dest);
                            std::process::exit(0);
                        }
                        _ => {
                            eprintln!(
                                "Unsupported file extension for DOT file, must be one of .txt, .gv, or .dot."
                            );
                            std::process::exit(1);
                        }
                    }
                }
                None => {
                    println!("{}", dot);
                    std::process::exit(0);
                }
            }
        }
        cmd => {
            eprintln!("Unknown subcommand {}", cmd);
            std::process::exit(1);
        }
    }
}

fn write_buffer<R: Renderer>(
    trace: &Trace,
    func: &str,
    indices: &[usize],
    packet_index: u32,
) -> Vec<u8> {
    if let Some(mut state) = R::register(trace, func) {
        let k = indices.partition_point(|&p| p <= packet_index as usize);
        state.seek(trace, indices, k);
        state.to_rgba()
    } else {
        Vec::new()
    }
}

fn write_reuse_distance_buffer(
    trace: &Trace,
    func: &str,
    store_indices: &[usize],
    load_indices: &[usize],
    packet_index: u32,
) -> Vec<u8> {
    if let Some(mut state) = ReuseDistanceState::new(trace, func) {
        let store_k = store_indices.partition_point(|&p| p <= packet_index as usize);
        let load_k = load_indices.partition_point(|&p| p <= packet_index as usize);
        state.seek(trace, store_indices, load_indices, store_k, load_k);
        state.to_rgba()
    } else {
        Vec::new()
    }
}
