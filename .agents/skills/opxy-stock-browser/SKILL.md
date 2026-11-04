---
name: opxy-stock-browser
description: Reuse the stock engine/preset browser for native OP-XY OS 1.2.1 SDK GUI selectors. Use for platform integrations; this is not a public machine or WASM menu API.
---

# OP-XY stock browser

Read [the stock browser contract](../../../docs/abi-1.2.1.md#stock-browser)
before integrating a selector. It owns the API example, lifetime rules,
validation limits and version-bound ABI.

- Use `StockSelector<N>` from the internal platform header. Keep selector storage
  and callback context alive through modal close; call `open` only on GUI.
- Give the adapter a title, labels, initial index and callbacks. Handle a false
  return as a rejected open. Keep highlighted and committed state separate.
- Let stock own rendering and navigation. Use the adapter's left/white confirm
  callbacks; do not copy stock ABI structures into clients or add global input
  hooks for selector controls.
- Keep feature-specific persistence and audio transport in the client. Do not
  share selector storage with audio callbacks.

When changing the adapter, extend its host boundary fixture for observable
behavior, including a second client and close/reopen paths. Follow the
repository's native build and release verification requirements. Use private
external firmware paths and preserve stock/tool authentication and exact
reconstruction. A host result does not establish a hardware observation.
