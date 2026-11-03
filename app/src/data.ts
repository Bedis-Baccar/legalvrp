import { useEffect, useState } from "react";
import type { Bundle, Gallery, Index } from "./types";

const cache = new Map<string, Promise<unknown>>();

export function fetchJson<T>(path: string): Promise<T> {
  if (!cache.has(path)) {
    cache.set(
      path,
      fetch(`data/${path}`, { cache: "no-store" }).then((r) => {
        if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
        return r.json();
      }),
    );
  }
  return cache.get(path) as Promise<T>;
}

export function useJson<T>(path: string | undefined): { data?: T; error?: string } {
  const [state, setState] = useState<{ data?: T; error?: string }>({});
  useEffect(() => {
    if (!path) return;
    let live = true;
    setState({});
    fetchJson<T>(path)
      .then((data) => live && setState({ data }))
      .catch((e: Error) => live && setState({ error: e.message }));
    return () => {
      live = false;
    };
  }, [path]);
  return state;
}

export const useIndex = () => useJson<Index>("index.json");
export const useBundle = (file?: string) => useJson<Bundle>(file);
export const useGallery = (file?: string) => useJson<Gallery>(file);

// Several bundles at once (comparisons): undefined until all are loaded.
export function useBundles(files: (string | undefined)[]): { data?: (Bundle | undefined)[]; error?: string } {
  const key = files.join("|");
  const [state, setState] = useState<{ data?: (Bundle | undefined)[]; error?: string }>({});
  useEffect(() => {
    let live = true;
    setState({});
    Promise.all(files.map((f) => (f ? fetchJson<Bundle>(f) : Promise.resolve(undefined))))
      .then((data) => live && setState({ data }))
      .catch((e: Error) => live && setState({ error: e.message }));
    return () => {
      live = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);
  return state;
}
