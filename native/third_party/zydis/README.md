# Zydis decoder-only amalgamation

Upstream: https://github.com/zyantific/zydis

- Zydis v4.1.1 commit `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`
- Zycore submodule commit `0b2432ced0884fd152b471d97ecf0258ff4d859f`
- MIT licenses: `LICENSE-Zydis` and `LICENSE-Zycore` in this directory.

The generated header is upstream's public header amalgamation. The generated C
file includes decoder sources only: Decoder, DecoderData, MetaInfo, Mnemonic,
Register, SharedData, Utils and Zydis. Encoder and formatter sources are omitted.
Absolute paths in generated source comments are normalized to `src/<filename>`.
No decoder implementation or instruction table was otherwise edited.

To reproduce, clone the pinned upstream revision with its Zycore submodule and
run `python native/tools/amalgamate_zydis.py <path-to-clone>` from any directory.
Python is a developer tool only; the shipped Windows EXE embeds the decoder.
