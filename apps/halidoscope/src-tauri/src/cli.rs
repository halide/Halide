use std::ffi::OsStr;
use std::path::Path;

use comfy_table::presets::UTF8_HORIZONTAL_ONLY;
use comfy_table::{presets, Table};
use serde_json::json;
use tauri_plugin_cli::SubcommandMatches;

use crate::commands::TraceMeta;
use crate::graph::to_dot;
use crate::render::{
    GrayscaleState, LoadFrequencyState, RedundantState, Renderer, ReuseDistanceState, RgbState,
    StoreFrequencyState,
};
use crate::trace::Trace;

pub fn halidoscope_cli(subcommand: Box<SubcommandMatches>) {
    match subcommand.name.as_str() {
        "dot" => dot(subcommand),
        "list" => list(subcommand),
        "snapshot" => snapshot(subcommand),
        "stats" => stats(subcommand),
        cmd => {
            eprintln!("Unknown subcommand {}", cmd);
            std::process::exit(1);
        }
    };
}

fn dot(subcommand: Box<SubcommandMatches>) -> Option<()> {
    let args = &subcommand.matches.args;

    let trace_path = args.get("trace").and_then(|a| a.value.as_str())?;
    let destination = args.get("destination").and_then(|a| a.value.as_str());

    // Load and parse the trace.
    let trace = Trace::load_from_file(trace_path).unwrap_or_else(|e| {
        eprintln!("Error loading trace: {}", e);
        std::process::exit(1);
    });
    let dot = to_dot(&trace.dag_edges);

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

fn list(subcommand: Box<SubcommandMatches>) -> Option<()> {
    let args = &subcommand.matches.args;

    let trace_path = args.get("trace").and_then(|a| a.value.as_str())?;

    // Load and parse the trace.
    let trace = Trace::load_from_file(trace_path).unwrap_or_else(|e| {
        eprintln!("Error loading trace: {}", e);
        std::process::exit(1);
    });

    let as_json = args
        .get("json")
        .and_then(|a| a.value.as_bool())
        .unwrap_or(false);

    println!("Printing Funcs for {}", trace_path);

    if as_json {
        let data = &trace
            .funcs
            .iter()
            .map(|(name, func)| {
                json!({
                    "func": name.clone(),
                    "dimensionality": func.min_coords.len()
                })
            })
            .collect::<Vec<_>>();

        let json = serde_json::to_string_pretty(&data).unwrap_or_else(|err| {
            eprintln!("Error serializing Funcs to JSON: {}", err);
            std::process::exit(1);
        });

        println!("{}", json);
    } else {
        let mut table = Table::new();

        table
            .load_preset(UTF8_HORIZONTAL_ONLY)
            .set_header(vec!["Func", "Dimensionality"]);

        for (func_name, func) in &trace.funcs {
            table.add_row(vec![func_name.clone(), func.min_coords.len().to_string()]);
        }

        println!("{table}");
    }

    std::process::exit(0);
}

fn stats(subcommand: Box<SubcommandMatches>) -> Option<()> {
    let args = &subcommand.matches.args;

    let trace_path = args.get("trace").and_then(|a| a.value.as_str())?;
    let func = args.get("func").and_then(|a| a.value.as_str());

    // Load and parse the trace.
    let trace = Trace::load_from_file(trace_path).unwrap_or_else(|e| {
        eprintln!("Error loading trace: {}", e);
        std::process::exit(1);
    });

    // Reuse the same derivation the GUI uses so the CLI reports identical stats.
    let meta = TraceMeta::from_trace(&trace);

    let funcs: Vec<_> = match func {
        Some(name) => {
            let matched: Vec<_> = meta.funcs.iter().filter(|f| f.name == name).collect();

            if matched.is_empty() {
                eprintln!(
                    "Func '{}' not found in trace. Available Funcs: {:?}",
                    name,
                    meta.funcs.iter().map(|f| &f.name).collect::<Vec<_>>()
                );
                std::process::exit(1);
            }

            matched
        }
        None => meta.funcs.iter().collect(),
    };

    let as_json = args.get("json").and_then(|a| a.value.as_bool())?;

    println!("Printing Func statistics for {}", trace_path);

    if as_json {
        let json = serde_json::to_string_pretty(&funcs).unwrap_or_else(|err| {
            eprintln!("Error serializing Func statistics to JSON: {}", err);
            std::process::exit(1);
        });
        println!("{}", json);
    } else {
        let mut table = Table::new();

        table
            .load_preset(presets::UTF8_HORIZONTAL_ONLY)
            .set_header(vec![
                "Func",
                "Minimum Coordinates",
                "Maximum Coordinates",
                "Minimum Value",
                "Maximum Value",
                "Maximum Store Count",
                "Maximum Load Count",
                "Thread Count",
            ]);

        for func in funcs {
            table.add_row(vec![
                func.name.clone(),
                format!(
                    "({})",
                    func.min_coords
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join(",")
                ),
                format!(
                    "({})",
                    func.max_coords
                        .iter()
                        .map(ToString::to_string)
                        .collect::<Vec<_>>()
                        .join(",")
                ),
                func.min_value.map(|v| v.to_string()).unwrap_or_default(),
                func.max_value.map(|v| v.to_string()).unwrap_or_default(),
                func.max_store_count.to_string(),
                func.max_load_count.to_string(),
                func.thread_count.to_string(),
            ]);
        }
        println!("{table}");
    }

    std::process::exit(0);
}

fn snapshot(subcommand: Box<SubcommandMatches>) -> Option<()> {
    let args = &subcommand.matches.args;

    let trace_path = args.get("trace").and_then(|a| a.value.as_str())?;
    let func = args.get("func").and_then(|a| a.value.as_str())?;
    let packet_index = args
        .get("packet-index")
        .and_then(|a| a.value.as_str())
        .and_then(|s| s.parse::<u32>().ok())
        .unwrap_or(0);
    let mode = args
        .get("mode")
        .and_then(|a| a.value.as_str())
        .unwrap_or("grayscale");
    let destination = args.get("destination").and_then(|a| a.value.as_str())?;

    // Load and parse the trace.
    let trace = Trace::load_from_file(trace_path).unwrap_or_else(|e| {
        eprintln!("Error loading trace: {}", e);
        std::process::exit(1);
    });

    // Find the target function.
    let target_func = trace.funcs.get(func)?;

    // Exit early if the packet index is out of bounds.
    if packet_index as usize >= trace.packets.len() {
        eprintln!(
            "Packet index {} is out of bounds. Valid range: 0..{}",
            packet_index,
            trace.packets.len()
        );
        std::process::exit(1);
    }

    let ext = Path::new(destination).extension().and_then(OsStr::to_str);
    let store_indices = trace.func_store_indices(&func)?;
    let load_indices = trace.func_load_indices(&func)?;
    let k = packet_index.try_into().unwrap_or_else(|_| {
        eprintln!("Packet index {} is too large.", packet_index);
        std::process::exit(1);
    });

    match ext {
        Some("json") => {
            // Obtain the values for the given rendering mode at the current packet index.
            let json = match mode {
                "grayscale" => {
                    let Some(mut gs) = GrayscaleState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    gs.seek(&trace, store_indices, k);
                    serde_json::to_string_pretty(&gs.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                "rgb" => {
                    let Some(mut rgbs) = RgbState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    rgbs.seek(&trace, store_indices, k);
                    serde_json::to_string_pretty(&rgbs.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                "store-frequency" => {
                    let Some(mut sfs) = StoreFrequencyState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    sfs.seek(&trace, store_indices, k);
                    serde_json::to_string_pretty(&sfs.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                "load-frequency" => {
                    let Some(mut lfs) = LoadFrequencyState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    lfs.seek(&trace, load_indices, k);
                    serde_json::to_string_pretty(&lfs.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                "redundant-stores" => {
                    let Some(mut rs) = RedundantState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    rs.seek(&trace, store_indices, k);
                    serde_json::to_string_pretty(&rs.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                "reuse-distance" => {
                    let Some(mut rds) = ReuseDistanceState::new(&trace, &target_func.name) else {
                        eprintln!("Func '{}' has no usable geometry.", target_func.name);
                        std::process::exit(1);
                    };

                    rds.seek(&trace, store_indices, load_indices, k, k);
                    serde_json::to_string_pretty(&rds.to_values()).unwrap_or_else(|e| {
                        eprintln!("Error serializing values to JSON: {}", e);
                        std::process::exit(1);
                    })
                }
                _ => {
                    eprintln!("Unsupported rendering mode '{}'.", mode);
                    std::process::exit(1);
                }
            };

            if let Err(e) = std::fs::write(destination, json) {
                eprintln!("Failed to write snapshot file: {}", e);
                std::process::exit(1);
            }

            println!("Snapshot written to {}", destination);
            std::process::exit(0);
        }
        _ => {
            eprintln!("Unsupported file extension for snapshot file, must be .json.");
            std::process::exit(1);
        }
    }
}
