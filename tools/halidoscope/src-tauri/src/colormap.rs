use palette::{Mix, Srgb};

/// The palette used for displaying core metrics in Halidoscope, including Store Frequency, Load
/// Frequency, Redundant Stores, and Reuse Distance.
pub const METRIC_PALETTE: [&str; 10] = [
    "#0078D1", "#1695F3", "#3DACFF", "#70C2FF", "#D6EEFF", "#FFE2D6", "#FFBFA3", "#FF773D",
    "#FA6400", "#D64000",
];

pub struct Colormap {
    stops: Vec<Srgb<f32>>,
}

impl Colormap {
    pub fn from_hex(hex_colors: &[&str]) -> Self {
        Self {
            stops: hex_colors.iter().map(|hex| parse_hex(hex)).collect(),
        }
    }

    /// Samples the gradient at `i / 255` for `i` in `[0, 255]` to build an LUT.
    pub fn to_lut(&self) -> [[u8; 3]; 256] {
        std::array::from_fn(|i| self.eval(i as f64 / 255.0))
    }

    fn eval(&self, t: f64) -> [u8; 3] {
        let segments = self.stops.len() - 1;
        let t = (t.clamp(0.0, 1.0) as f32) * segments as f32;
        let i = (t.floor() as usize).min(segments - 1);
        let local_t = t - i as f32;

        let color: Srgb<u8> = self.stops[i].mix(self.stops[i + 1], local_t).into_format();
        [color.red, color.green, color.blue]
    }
}

fn parse_hex(hex: &str) -> Srgb<f32> {
    let hex = hex.trim_start_matches('#');
    let r = u8::from_str_radix(&hex[0..2], 16).expect("valid hex color");
    let g = u8::from_str_radix(&hex[2..4], 16).expect("valid hex color");
    let b = u8::from_str_radix(&hex[4..6], 16).expect("valid hex color");

    Srgb::new(r, g, b).into_format()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_d3_piecewise_reference() {
        let cmap = Colormap::from_hex(&METRIC_PALETTE);
        let cases: [(f64, &str); 11] = [
            (0.0, "#0078d1"),
            (0.01, "#027bd4"),
            (0.12, "#1997f4"),
            (0.33, "#6ec1ff"),
            (0.495, "#e9e9ec"),
            (0.50, "#ebe8eb"),
            (0.505, "#ece7e9"),
            (0.66, "#ffc1a6"),
            (0.87, "#fb670a"),
            (0.99, "#d94300"),
            (1.0, "#d64000"),
        ];

        for (t, expected) in cases {
            let [r, g, b] = cmap.eval(t);
            let got = format!("#{r:02x}{g:02x}{b:02x}");
            assert_eq!(got, expected, "t={t}");
        }
    }
}
