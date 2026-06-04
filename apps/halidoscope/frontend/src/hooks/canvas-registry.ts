import * as React from "react";

import { FuncUpdate } from "../types";

/**
 * The imperative draw surface a {@link FuncCanvas} exposes to the bus. The
 * canvas owns its persistent pixel buffer so that store writes accumulate
 * across incremental range updates.
 */
export interface CanvasHandle {
  /** Apply a range's pixel writes on top of the existing buffer. */
  draw: (update: FuncUpdate) => void;
  /** Reset the buffer to empty (used on backward scrubs / new traces). */
  clear: () => void;
}

/**
 * Routes backend {@link FuncUpdate}s to the matching {@link FuncCanvas} without
 * pushing pixel data through React state. Canvases register a {@link CanvasHandle}
 * keyed by their qualified func name (the node id); the registry resolves a
 * packet's raw func name to that key using the same matching the backend uses.
 */
export class CanvasRegistry {
  private handlers = new Map<string, CanvasHandle>();
  // raw packet func name -> resolved qualified handler key (or null if none).
  private resolveCache = new Map<string, string | null>();

  /** Register a canvas under its qualified func name; returns an unregister fn. */
  register(qualifiedName: string, handle: CanvasHandle): () => void {
    this.handlers.set(qualifiedName, handle);
    this.resolveCache.clear();

    return () => {
      if (this.handlers.get(qualifiedName) === handle) {
        this.handlers.delete(qualifiedName);
        this.resolveCache.clear();
      }
    };
  }

  /**
   * Resolve a raw func name (e.g. "f0") to a registered qualified key (e.g.
   * "local_laplacian:f0"). Mirrors the backend's `_get_func_item_for_packet`:
   * exact match, then a "pipeline:func" suffix, then a substring fallback.
   */
  private resolve(raw: string): string | null {
    const cached = this.resolveCache.get(raw);
    if (cached !== undefined) return cached;

    let match: string | null = null;
    if (this.handlers.has(raw)) {
      match = raw;
    } else {
      for (const name of this.handlers.keys()) {
        if (name.endsWith(`:${raw}`) || name.includes(raw)) {
          match = name;
          break;
        }
      }
    }

    this.resolveCache.set(raw, match);

    return match;
  }

  /** Route one func's updates to its canvas, if a matching one is registered. */
  dispatch(update: FuncUpdate): void {
    const key = this.resolve(update.func);
    if (key) this.handlers.get(key)?.draw(update);
  }

  /** Clear every registered canvas (backward scrub / re-render from scratch). */
  clearAll(): void {
    for (const handle of this.handlers.values()) handle.clear();
  }
}

const CanvasRegistryContext = React.createContext<CanvasRegistry | null>(null);
export const CanvasRegistryProvider = CanvasRegistryContext.Provider;

/** Access the {@link CanvasRegistry} provided by an ancestor {@link CanvasRegistryProvider}. */
export function useCanvasRegistry(): CanvasRegistry {
  const bus = React.useContext(CanvasRegistryContext);

  if (!bus) {
    throw new Error(
      "useCanvasRegistry must be used within a CanvasRegistryProvider",
    );
  }

  return bus;
}
