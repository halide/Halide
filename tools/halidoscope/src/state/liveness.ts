import { atom } from "jotai";

export const livenessAtom = atom<{ active: boolean }>({ active: false });

export const LIVENESS_COLORS = {
  realization: "var(--color-oxide-yellow)",
  production: "var(--color-oxide-green)",
  consumption: "var(--color-oxide-purple)",
};

export const LIVENESS_LEGEND = [
  { color: LIVENESS_COLORS.realization, label: "In Realization" },
  { color: LIVENESS_COLORS.production, label: "Producing" },
  { color: LIVENESS_COLORS.consumption, label: "Consuming" },
];
