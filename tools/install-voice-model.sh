#!/usr/bin/env bash
set -euo pipefail

model_id=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30
model_archive="$model_id.tar.bz2"
model_sha256=5a2832047ea1f97dd0dc595b816c230c4bafad65cfc0341fa57517cadc50afd0
runtime_version=v1.13.2
runtime_archive=sherpa-onnx-v1.13.2-linux-x64-shared-no-tts-lib.tar.bz2
runtime_sha256=1c66f4ec57cbf6a608f09e373796346943702251f75d08c45e8f47345a960ee6
vad_sha256=9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6
punctuation_id=sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8
punctuation_archive="$punctuation_id.tar.bz2"
punctuation_sha256=c0d5aa5f8eeb686032345e180bedf39319dc2e0556781c6264bcadba8328a6e1
data_home="${XDG_DATA_HOME:-$HOME/.local/share}/modern-ime"
model_root="$data_home/models"
runtime_root="$data_home/runtime"
temp_root=$(mktemp -d)
trap 'rm -rf "$temp_root"' EXIT

if [[ $(uname -m) != x86_64 ]]; then
  printf 'This runtime package supports x86_64 only.\n' >&2
  exit 2
fi

mkdir -p "$model_root" "$runtime_root"
download_and_verify() {
  local url=$1 expected=$2 target=$3
  curl --fail --location --continue-at - --retry 3 --retry-all-errors --retry-delay 2 --output "$target" "$url"
  printf '%s  %s\n' "$expected" "$target" | sha256sum --check --status
}
install_tree() {
  local source=$1 target=$2 label=$3
  if [[ -d $target ]]; then
    local stamp
    stamp=$(date -u +%Y%m%dT%H%M%SZ)
    mv "$target" "${target}.previous-$stamp"
    printf 'Existing %s moved to %s.previous-%s\n' "$label" "$target" "$stamp"
  fi
  mv "$source" "$target"
}

runtime_target="$runtime_root/sherpa-onnx-v1.13.2"
if [[ -f $runtime_target/lib/libsherpa-onnx-c-api.so && -f $runtime_target/lib/libonnxruntime.so ]] && rg -q "$runtime_sha256" "$runtime_target/runtime.json"; then
  printf 'Verified offline ASR runtime is already installed.\n'
else
  printf 'Downloading verified offline ASR runtime (sherpa-onnx %s)…\n' "$runtime_version"
  download_and_verify "https://github.com/k2-fsa/sherpa-onnx/releases/download/$runtime_version/$runtime_archive" "$runtime_sha256" "$temp_root/$runtime_archive"
  mkdir "$temp_root/runtime"
  tar -xjf "$temp_root/$runtime_archive" -C "$temp_root/runtime"
  runtime_source="$temp_root/runtime/sherpa-onnx-v1.13.2-linux-x64-shared-no-tts-lib"
  [[ -f $runtime_source/lib/libsherpa-onnx-c-api.so && -f $runtime_source/lib/libonnxruntime.so ]]
  printf '{"format":"modern-ime-runtime","sherpa_onnx_version":"1.13.2","archive_sha256":"%s","license":"Apache-2.0"}\n' "$runtime_sha256" > "$runtime_source/runtime.json"
  install_tree "$runtime_source" "$runtime_target" runtime
fi

model_target="$model_root/$model_id"
if [[ -f $model_target/encoder.int8.onnx && -f $model_target/decoder.onnx && -f $model_target/joiner.int8.onnx && -f $model_target/tokens.txt ]] && rg -q "$model_sha256" "$model_target/model.json"; then
  printf 'Verified offline ASR model is already installed.\n'
else
  printf 'Downloading verified 2025 Mandarin ASR model (~127 MB)…\n'
  download_and_verify "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$model_archive" "$model_sha256" "$temp_root/$model_archive"
  mkdir "$temp_root/model"
  tar -xjf "$temp_root/$model_archive" -C "$temp_root/model"
  model_source="$temp_root/model/$model_id"
  [[ -f $model_source/encoder.int8.onnx && -f $model_source/decoder.onnx && -f $model_source/joiner.int8.onnx && -f $model_source/tokens.txt ]]
  install_tree "$model_source" "$model_target" model
fi
printf '{"format":"modern-ime-model","id":"%s","kind":"transducer","modeling_unit":"cjkchar","supports_hotwords":true,"files":{"encoder":"encoder.int8.onnx","decoder":"decoder.onnx","joiner":"joiner.int8.onnx","tokens":"tokens.txt"},"sherpa_onnx_version":"1.13.2","archive_sha256":"%s","source":"https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models","upstream":"https://huggingface.co/yuekai/icefall-asr-multi-zh-hans-zipformer-large","license":"REVIEW_REQUIRED"}\n' "$model_id" "$model_sha256" > "$model_target/model.json"

vad_target="$model_root/silero-vad"
if [[ -f $vad_target/silero_vad.onnx ]] && rg -q "$vad_sha256" "$vad_target/model.json"; then
  printf 'Verified Silero VAD model is already installed.\n'
else
  printf 'Downloading verified Silero VAD model…\n'
  mkdir "$temp_root/vad"
  download_and_verify "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx" "$vad_sha256" "$temp_root/vad/silero_vad.onnx"
  printf '{"format":"modern-ime-model","id":"silero-vad","sha256":"%s","source":"https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models","license":"MIT"}\n' "$vad_sha256" > "$temp_root/vad/model.json"
  install_tree "$temp_root/vad" "$vad_target" VAD
fi

punctuation_target="$model_root/$punctuation_id"
if [[ -f $punctuation_target/model.int8.onnx ]] && rg -q "$punctuation_sha256" "$punctuation_target/model.json"; then
  printf 'Verified Chinese-English punctuation model is already installed.\n'
else
  printf 'Downloading verified INT8 Chinese-English punctuation model (~62 MB)…\n'
  download_and_verify "https://github.com/k2-fsa/sherpa-onnx/releases/download/punctuation-models/$punctuation_archive" "$punctuation_sha256" "$temp_root/$punctuation_archive"
  mkdir "$temp_root/punctuation"
  tar -xjf "$temp_root/$punctuation_archive" -C "$temp_root/punctuation"
  punctuation_source="$temp_root/punctuation/$punctuation_id"
  [[ -f $punctuation_source/model.int8.onnx ]]
  install_tree "$punctuation_source" "$punctuation_target" punctuation
fi
printf '{"format":"modern-ime-model","id":"%s","archive_sha256":"%s","source":"https://github.com/k2-fsa/sherpa-onnx/releases/tag/punctuation-models","upstream":"https://modelscope.cn/models/iic/punc_ct-transformer_zh-cn-common-vocab272727-pytorch/summary","license":"Apache-2.0"}\n' "$punctuation_id" "$punctuation_sha256" > "$punctuation_target/model.json"

printf 'Offline ASR model installed at %s\n' "$model_root/$model_id"
