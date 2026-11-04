// Muting is independent of engine activation and the user's volume setting.
export function createOutputMute() {
  let muted = false, parameter = null, context = null;
  return {
    get muted() { return muted; },
    attach(nextParameter, nextContext) {
      parameter = nextParameter;
      context = nextContext;
      parameter.setValueAtTime(muted ? 0 : 1, context.currentTime);
    },
    detach() { parameter = context = null; },
    toggle() {
      muted = !muted;
      if (parameter && context.state !== 'closed') {
        parameter.setTargetAtTime(muted ? 0 : 1, context.currentTime, 0.01);
      }
      return muted;
    },
  };
}
