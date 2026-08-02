#!/usr/bin/env bash
set -euo pipefail

model_id=sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20
model_archive="$model_id.tar.bz2"
model_sha256=27ffbd9ee24ad186d99acc2f6354d7992b27bcab490812510665fa8f9389c5f8
runtime_version=v1.13.2
runtime_archive=sherpa-onnx-v1.13.2-linux-x64-shared-no-tts-lib.tar.bz2
runtime_sha256=1c66f4ec57cbf6a608f09e373796346943702251f75d08c45e8f47345a960ee6
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
if [[ -f $model_target/encoder-epoch-99-avg-1.int8.onnx && -f $model_target/decoder-epoch-99-avg-1.onnx && -f $model_target/joiner-epoch-99-avg-1.int8.onnx && -f $model_target/tokens.txt ]] && rg -q "$model_sha256" "$model_target/model.json"; then
  printf 'Verified offline ASR model is already installed.\n'
else
  printf 'Downloading verified offline bilingual Chinese/English ASR model (~511 MB)…\n'
  download_and_verify "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$model_archive" "$model_sha256" "$temp_root/$model_archive"
  mkdir "$temp_root/model"
  tar -xjf "$temp_root/$model_archive" -C "$temp_root/model"
  model_source="$temp_root/model/$model_id"
  [[ -f $model_source/encoder-epoch-99-avg-1.int8.onnx && -f $model_source/decoder-epoch-99-avg-1.onnx && -f $model_source/joiner-epoch-99-avg-1.int8.onnx && -f $model_source/tokens.txt ]]
  printf '{"format":"modern-ime-model","id":"%s","sherpa_onnx_version":"1.13.2","archive_sha256":"%s","source":"https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models","license":"upstream model terms"}\n' "$model_id" "$model_sha256" > "$model_source/model.json"
  install_tree "$model_source" "$model_target" model
fi

printf 'Offline ASR model installed at %s\n' "$model_root/$model_id"
