// Keep the user gesture synchronous while audio starts asynchronously.
// A cancelled or superseded request must never play when startup finishes.
export function createAudioActivation({ isReady, enableAudio }) {
  const pending = new Map();
  return {
    isPending: source => pending.has(source),
    cancel: source => pending.delete(source),
    cancelAll: () => pending.clear(),
    async run(source, play) {
      if (pending.has(source)) return false;
      if (isReady()) { play(); return true; }
      const request = {};
      pending.set(source, request);
      let enabled = false;
      try { enabled = await enableAudio(); } catch { /* The audio owner reports startup errors. */ }
      if (pending.get(source) !== request) return false;
      pending.delete(source);
      if (!enabled) return false;
      play();
      return true;
    },
  };
}
