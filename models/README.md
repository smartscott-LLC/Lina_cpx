# models/

Local host-model files (e.g. `llama.gguf`) for the symbiote compute driver live here.

- The default path is `./models/llama.gguf` (see `LinaConfig.model_path`).
- Everything in this directory is gitignored — model binaries are never committed.
- The host model is an **unprivileged subordinate compute driver** (Invariant 4). It is
  loaded through `host_model_adapter.hpp` and never touches the egress socket or UI directly.
