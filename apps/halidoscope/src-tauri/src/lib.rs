pub mod commands;
pub mod render;
pub mod trace;

// Learn more about Tauri commands at https://tauri.app/develop/calling-rust/
#[tauri::command]
fn get_cwd() -> Result<String, String> {
    std::env::current_dir()
        .map_err(|e| e.to_string())
        .and_then(|p| {
            p.parent()
                .map(|parent| parent.to_string_lossy().into_owned())
                .ok_or_else(|| "no parent directory".to_string())
        })
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|app| {
            #[cfg(desktop)]
            app.handle().plugin(tauri_plugin_cli::init())?;
            Ok(())
        })
        .plugin(tauri_plugin_opener::init())
        .manage(commands::AppState::default())
        .invoke_handler(tauri::generate_handler![
            get_cwd,
            commands::open_trace,
            commands::render_grayscale,
            commands::render_rgb,
            commands::render_store_frequency,
            commands::render_load_frequency,
            commands::render_redundant_stores,
            commands::render_reuse_distance
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
