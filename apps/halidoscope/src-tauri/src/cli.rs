use std::ffi::OsStr;
use std::path::Path;

use tauri_plugin_cli::SubcommandMatches;

use crate::graph::to_dot;
use crate::trace::Trace;

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
            let _mode = args
                .get("mode")
                .and_then(|a| a.value.as_str())
                .unwrap_or("grayscale");
            let _destination = args
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
            let _target_func = tr.funcs.get(func).unwrap_or_else(|| {
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

            // TODO: Convert this command to write out numeric values to CSV / JSON.
            std::process::exit(0);
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
